/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM miniport registration, adapter PnP/Power lifecycle,
 *              and DxgkCb* callbacks exported to miniport drivers
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Overview
 * --------
 * This file implements the "port" half of the WDDM miniport/port driver split,
 * mirroring the role that videoprt.c plays for XDDM miniports and fdo.c plays
 * for storport miniports.
 *
 * Two-level object model
 * ----------------------
 *   DXGKRNL_MINIPORT_CONTEXT   — allocated as a DriverObjectExtension for the
 *       miniport's own DRIVER_OBJECT.  Created by DxgkInitializeEx; lives as
 *       long as the miniport is loaded.  Holds the DDI callback table copy and
 *       the canonical registry path.
 *
 *   DXGKRNL_ADAPTER            — stored in the FDO's DeviceExtension.  One
 *       instance per physical GPU.  Created by DxgkpAddDevice; destroyed by
 *       DxgkAdapterRemove.
 *
 * Dispatch hooking
 * ----------------
 * DxgkInitializeEx installs four callbacks into the miniport's DriverObject:
 *
 *   DriverExtension->AddDevice           ← DxgkpAddDevice
 *   MajorFunction[IRP_MJ_PNP]           ← DxgkpMiniportPnpDispatch
 *   MajorFunction[IRP_MJ_POWER]         ← DxgkpMiniportPowerDispatch
 *   DriverUnload                         ← DxgkpDriverUnload
 *
 * x86/amd64 memory ordering notes
 * --------------------------------
 * The adapter State field is read/written under AdapterMutex (KMUTEX at
 * APC_LEVEL) from PASSIVE_LEVEL paths and is updated only in the PnP dispatch
 * which is serialised by the I/O manager.  No additional barriers are needed
 * for State.
 *
 * InterruptLock is shared with the ISR at its synchronize IRQL.  A DPC or
 * PASSIVE_LEVEL lifecycle path raises to that IRQL before acquiring the lock;
 * taking it at plain DISPATCH_LEVEL would let the ISR preempt the owner and
 * deadlock on the same lock.  The spinlock acquire/release barriers provide
 * the required cross-architecture visibility.
 *
 * The one-time init guard (DxgkpInitialized) uses InterlockedCompareExchange
 * which on x86-64 compiles to LOCK CMPXCHG — a fully-ordered instruction.
 * No additional fences are required.
 */

/* dxgkrnl_private.h includes NDEBUG, <debug.h>, and "debug.h" via PCH. */
#include "dxgkrnl_private.h"
#include "vidmm.h"
#include "vidsch.h"
#include "present.h"
#include "pnp.h"
#include "context.h"
#include "dxgmms2_client.h"
#include "submit_reservation_core.h"
#include "hotplug_work_core.h"
#include "adapter_map_core.h"
#include "adapter_start_core.h"

#include <ndk/inbvfuncs.h>
#include <drivers/acpi/acpisystem.h>
#include <reactos/arc/arc.h>
#include <reactos/drivers/acpi/acpipci.h>
#include <reactos/drivers/cmreslist.h>
#define DXGKP_BUGCHECK_VIDEO_DXGKRNL_FATAL_ERROR 0x113
#define DXGKP_FATAL_SURPRISE_REMOVAL_SUBTYPE 0x19
#define DXGKP_FATAL_MMS2_LIFECYCLE_SUBTYPE 0x1A
/* The public DDI mandates a bugcheck but does not publish the native subtype. */
#define DXGKP_FATAL_PHYSICAL_MEMORY_ADL_LEAK_SUBTYPE 0x524F5301UL
#define DXGKP_GPUMMU_END_TO_END 1
#define DXGKP_MMS2_FAILURE_ADD_ROLLBACK 1
#define DXGKP_MMS2_FAILURE_ATTACH_ROLLBACK 2
#define DXGKP_MMS2_FAILURE_FINAL_RETIREMENT 3
#define DXGKP_MMS2_FAILURE_FINAL_DESTROY 4
#define DXGKP_MINIPORT_CONTEXT_SIGNATURE 'MkgD'
#define DXGKP_DIAGNOSTIC_BUFFER_SIZE 0x80000UL
#define DXGKP_DMA_BUFFER_CACHE_LIMIT 16
#define DXGKP_DMA_BUFFER_CACHE_MAX_CAPACITY (4 * 1024 * 1024)
/* Retain more small buffers within the former four-by-4 MiB backing limit. */
#define DXGKP_DMA_BUFFER_CACHE_MAX_BYTES (16 * 1024 * 1024)

/* ========================================================================
 * InbV forward declarations
 *
 * The GOP helpers are exported by ntoskrnl but not declared in public SDK
 * headers.  Forward-declare only those helpers here; display-ownership
 * functions come from the NDK InbV interface.
 * ====================================================================== */

BOOLEAN
NTAPI
InbvHasValidGopFrameBuffer(VOID);

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

/* ========================================================================
 * POST (boot) display ownership
 *
 * Windows semantics: exactly one adapter owns the firmware boot display at
 * a time.  The basic-display fallback (softgpu, the MSBDD equivalent) holds
 * it only until a real miniport acquires it through
 * DxgkCbAcquirePostDisplayOwnership, at which point dxgkrnl stops the
 * fallback adapter (the MSBDD handover).  Conversely, when a real miniport
 * already owns the boot display, an acquire from the fallback returns an
 * empty descriptor so its StartDevice declines and the fallback devnode is
 * torn down.
 * ====================================================================== */
static KSPIN_LOCK g_PostDisplayOwnerLock;
static KMUTEX g_PostDisplayOwnershipMutex;
static KMUTEX g_MiniportRegistrationMutex;
static UCHAR g_MiniportContextClientId;
static PDXGKRNL_ADAPTER g_PostDisplayOwnerAdapter = NULL;

static NTSTATUS
DxgkpAdapterStopInternal(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN ReleasePostDisplayOwnership,
    _In_ DXGMMS2_STOP_REASON StopReason,
    _Out_opt_ PDXGK_DISPLAY_INFORMATION ReleasedPostDisplayInformation,
    _Out_opt_ PBOOLEAN ReleasedByDriver);

static NTSTATUS
DxgkpStopMiniportForTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter);

static NTSTATUS
DxgkpSetVsyncInterruptState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_CRTC_VSYNC_STATE VsyncState);

static SIZE_T
DxgkpResourceListSize(
    _In_ PCM_RESOURCE_LIST ResourceList)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    SIZE_T TotalSize;
    ULONG FullIndex;

    TotalSize = FIELD_OFFSET(CM_RESOURCE_LIST, List);
    FullDescriptor = &ResourceList->List[0];
    for (FullIndex = 0; FullIndex < ResourceList->Count; ++FullIndex)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
        SIZE_T FullSize;
        ULONG PartialIndex;

        FullSize = FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) + FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors);
        PartialDescriptor = &FullDescriptor->PartialResourceList.PartialDescriptors[0];
        for (PartialIndex = 0; PartialIndex < FullDescriptor->PartialResourceList.Count; ++PartialIndex)
        {
            SIZE_T DescriptorSize = sizeof(*PartialDescriptor);

            if (PartialDescriptor->Type == CmResourceTypeDeviceSpecific)
            {
                if (PartialDescriptor->u.DeviceSpecificData.DataSize > MAXULONG_PTR - DescriptorSize)
                    return 0;
                DescriptorSize += PartialDescriptor->u.DeviceSpecificData.DataSize;
            }
            if (FullSize > MAXULONG_PTR - DescriptorSize)
                return 0;
            FullSize += DescriptorSize;
            PartialDescriptor = CmiGetNextPartialDescriptor(PartialDescriptor);
        }
        if (TotalSize > MAXULONG_PTR - FullSize)
            return 0;
        TotalSize += FullSize;
        FullDescriptor = CmiGetNextResourceDescriptor(FullDescriptor);
    }
    return TotalSize;
}

static PCM_RESOURCE_LIST
DxgkpCloneResourceList(
    _In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    PCM_RESOURCE_LIST Clone;
    SIZE_T Size;

    if (ResourceList == NULL)
        return NULL;
    Size = DxgkpResourceListSize(ResourceList);
    if (Size == 0)
        return NULL;
    Clone = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_DXGK_RESOURCES);
    if (Clone != NULL)
        RtlCopyMemory(Clone, ResourceList, Size);
    return Clone;
}

static VOID
DxgkpReleaseAdapterResources(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter->AllocatedResources != NULL)
    {
        ExFreePoolWithTag(Adapter->AllocatedResources, TAG_DXGK_RESOURCES);
        Adapter->AllocatedResources = NULL;
    }
    if (Adapter->TranslatedResources != NULL)
    {
        ExFreePoolWithTag(Adapter->TranslatedResources, TAG_DXGK_RESOURCES);
        Adapter->TranslatedResources = NULL;
    }
}

static NTSTATUS
DxgkpCaptureAdapterResources(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PCM_RESOURCE_LIST AllocatedResources,
    _In_opt_ PCM_RESOURCE_LIST TranslatedResources)
{
    ASSERT(Adapter->AllocatedResources == NULL);
    ASSERT(Adapter->TranslatedResources == NULL);

    Adapter->AllocatedResources = DxgkpCloneResourceList(AllocatedResources);
    if (AllocatedResources != NULL && Adapter->AllocatedResources == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Adapter->TranslatedResources = DxgkpCloneResourceList(TranslatedResources);
    if (TranslatedResources != NULL && Adapter->TranslatedResources == NULL)
    {
        DxgkpReleaseAdapterResources(Adapter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_SUCCESS;
}

/* The published owner holds one FDO reference. A transient reference returned
 * here keeps both the device extension and Owner valid after dropping the lock. */
static PDXGKRNL_ADAPTER
DxgkpReferencePostDisplayOwner(
    _Out_ PDEVICE_OBJECT *OwnerDeviceObject)
{
    PDXGKRNL_ADAPTER Owner;
    PDEVICE_OBJECT DeviceObject = NULL;
    KIRQL OldIrql;

    *OwnerDeviceObject = NULL;
    KeAcquireSpinLock(&g_PostDisplayOwnerLock, &OldIrql);
    Owner = g_PostDisplayOwnerAdapter;
    if (Owner != NULL)
    {
        DeviceObject = Owner->FunctionalDeviceObject;
        if (DeviceObject != NULL)
            ObReferenceObject(DeviceObject);
        else
            Owner = NULL;
    }
    KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
    *OwnerDeviceObject = DeviceObject;
    return Owner;
}

static VOID
DxgkpSetPostDisplayOwner(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_ADAPTER PreviousOwner;
    PDEVICE_OBJECT NewDeviceObject;
    PDEVICE_OBJECT ReleasedDeviceObject = NULL;
    KIRQL OldIrql;

    if (Adapter == NULL || Adapter->FunctionalDeviceObject == NULL)
        return;
    NewDeviceObject = Adapter->FunctionalDeviceObject;
    ObReferenceObject(NewDeviceObject);
    KeAcquireSpinLock(&g_PostDisplayOwnerLock, &OldIrql);
    PreviousOwner = g_PostDisplayOwnerAdapter;
    if (PreviousOwner == Adapter)
    {
        KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
        ObDereferenceObject(NewDeviceObject);
        return;
    }
    if (PreviousOwner != NULL)
        ReleasedDeviceObject = PreviousOwner->FunctionalDeviceObject;
    g_PostDisplayOwnerAdapter = Adapter;
    KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
    if (ReleasedDeviceObject != NULL)
        ObDereferenceObject(ReleasedDeviceObject);
}

/* Called from stop/remove so a dead adapter never stays published. */
static VOID
DxgkpClearPostDisplayOwner(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDEVICE_OBJECT ReleasedDeviceObject = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&g_PostDisplayOwnerLock, &OldIrql);
    if (g_PostDisplayOwnerAdapter == Adapter)
    {
        ReleasedDeviceObject = Adapter->FunctionalDeviceObject;
        g_PostDisplayOwnerAdapter = NULL;
    }
    KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
    if (ReleasedDeviceObject != NULL)
        ObDereferenceObject(ReleasedDeviceObject);
}

/*
 * Stop the adapter currently holding the boot display so a new claimant can
 * take over.  Uses the documented Win8 handover DDI
 * (DxgkDdiStopDeviceAndReleasePostDisplayOwnership) when the owner
 * implements it, then runs the generic adapter stop (which unregisters the
 * \Device\VideoN display device so the claimant can register its own).
 */
static NTSTATUS
DxgkpStopPostDisplayOwner(
    _In_ PDXGKRNL_ADAPTER Owner,
    _Out_ PDXGK_DISPLAY_INFORMATION ReleasedPostDisplayInformation,
    _Out_ PBOOLEAN ReleasedByDriver)
{
    NTSTATUS Status;

    RtlZeroMemory(ReleasedPostDisplayInformation,
                  sizeof(*ReleasedPostDisplayInformation));
    *ReleasedByDriver = FALSE;
    DXGKRNL_WARN("DxgkpStopPostDisplayOwner: stopping %s adapter %p — a new miniport is acquiring the boot display\n", (Owner->MiniportContext != NULL && Owner->MiniportContext->IsBasicDisplayFallback) ? "basic-display fallback" : "display", Owner);
    Status = DxgkpAdapterStopInternal(Owner,
                                      TRUE,
                                      Dxgmms2StopReasonPnpStop,
                                      ReleasedPostDisplayInformation,
                                      ReleasedByDriver);
    if (NT_SUCCESS(Status))
        DxgkpClearPostDisplayOwner(Owner);
    return Status;
}

static VOID
DxgkpSetBasicDisplayUiSuppressed(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN Suppressed)
{
    LONG Previous;

    ASSERT(Adapter != NULL);
    ASSERT(Adapter->MiniportContext != NULL);
    ASSERT(Adapter->MiniportContext->IsBasicDisplayFallback);

    Previous = InterlockedExchange(&Adapter->BasicDisplayUiSuppressed,
                                   Suppressed ? 1 : 0);
    if ((Previous != 0) == (Suppressed != FALSE))
        return;

    IoInvalidateDeviceState(Adapter->PhysicalDeviceObject);
    DXGKRNL_INFO("BASICDISPLAY_UI: adapter %p device UI %s\n",
                 Adapter,
                 Suppressed ? "suppressed" : "restored");
}

/*
 * Stop and retain a BasicDisplay owner as one rollback transaction bound to
 * Claimant.  The caller serializes ownership with
 * g_PostDisplayOwnershipMutex and supplies a referenced owner FDO.  On
 * success that reference and RemoveRundownRef are transferred to Claimant
 * until DxgkpCompletePostDisplayHandoff commits or rolls the transaction
 * back.
 */
static NTSTATUS
DxgkpRetainAndStopBasicDisplayFallback(
    _In_ PDXGKRNL_ADAPTER Claimant,
    _In_ PDXGKRNL_ADAPTER Owner,
    _Inout_ PDEVICE_OBJECT *OwnerDeviceObject,
    _Out_ PDXGK_DISPLAY_INFORMATION ReleasedPostDisplayInformation,
    _Out_ PBOOLEAN ReleasedByDriver)
{
    BOOLEAN Armed;
    NTSTATUS RestoreStatus;
    NTSTATUS Status;

    ASSERT(Claimant != NULL);
    ASSERT(Claimant->MiniportContext != NULL);
    ASSERT(!Claimant->MiniportContext->IsBasicDisplayFallback);
    ASSERT(Owner != NULL);
    ASSERT(Owner->MiniportContext != NULL);
    ASSERT(Owner->MiniportContext->IsBasicDisplayFallback);
    ASSERT(OwnerDeviceObject != NULL);
    ASSERT(*OwnerDeviceObject != NULL);

    if (Claimant->PostDisplayHandoffCore.FallbackStopped ||
        Claimant->PostDisplayFallbackAdapter != NULL ||
        Claimant->PostDisplayFallbackDeviceObject != NULL ||
        Claimant->PostDisplayFallbackRemoveRundownHeld)
    {
        DXGKRNL_ERR("POSTDISPLAY_HANDOFF: claimant %p already has a pending "
                    "BasicDisplay fallback transaction\n",
                    Claimant);
        return STATUS_DEVICE_BUSY;
    }

    if (!ExAcquireRundownProtection(&Owner->RemoveRundownRef))
    {
        DXGKRNL_WARN("POSTDISPLAY_HANDOFF: BasicDisplay adapter %p is being "
                     "removed; claimant %p cannot take ownership\n",
                     Owner,
                     Claimant);
        return STATUS_DELETE_PENDING;
    }

    Status = DxgkpStopPostDisplayOwner(Owner,
                                       ReleasedPostDisplayInformation,
                                       ReleasedByDriver);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("POSTDISPLAY_HANDOFF: BasicDisplay adapter %p could not "
                    "be stopped 0x%08lX\n",
                    Owner,
                    Status);
        ExReleaseRundownProtection(&Owner->RemoveRundownRef);
        return Status;
    }

    Armed = DxgkPostDisplayCoreArm(&Claimant->PostDisplayHandoffCore);
    ASSERT(Armed);
    if (!Armed)
    {
        RestoreStatus = DxgkAdapterStart(Owner, NULL, NULL);
        DXGKRNL_ERR("POSTDISPLAY_HANDOFF: could not arm claimant %p; "
                    "BasicDisplay restore returned 0x%08lX\n",
                    Claimant,
                    RestoreStatus);
        ExReleaseRundownProtection(&Owner->RemoveRundownRef);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Claimant->PostDisplayFallbackAdapter = Owner;
    Claimant->PostDisplayFallbackDeviceObject = *OwnerDeviceObject;
    Claimant->PostDisplayFallbackRemoveRundownHeld = TRUE;
    *OwnerDeviceObject = NULL;
    /* Native DpiFdoStartAdapterThreadImpl disables MSBDD at this point and
     * enables it again on the failure path.  Do the same before the rest of
     * claimant startup, because display registration may remain pending while
     * the real adapter is already driving the desktop. */
    DxgkpSetBasicDisplayUiSuppressed(Owner, TRUE);
    return STATUS_SUCCESS;
}

/*
 * ReactOS exposes one XPDM-style win32ss display bridge.  A miniport owns that
 * bridge only after it has claimed POST display ownership through the public
 * callback.  A full WDDM adapter that deliberately starts without claiming
 * POST remains available through its adapter interface, while BasicDisplay (or
 * another real owner) keeps the desktop bridge.
 */
/* Defined below; the fallback handover is driven from adapter start. */
static NTSTATUS
DxgkpAcquirePostDisplayOwnership(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInformation,
    _Out_opt_ PDXGK_DISPLAY_OWNERSHIP_FLAGS Flags);

static BOOLEAN
DxgkpShouldRegisterDisplayBridge(
    _In_ PDXGKRNL_ADAPTER Claimant)
{
    PDEVICE_OBJECT OwnerDeviceObject;
    PDXGKRNL_ADAPTER Owner;
    BOOLEAN RegisterDisplayBridge;
    BOOLEAN TakeOverFromFallback;

    PAGED_CODE();

    if (Claimant->MiniportContext == NULL ||
        Claimant->MiniportContext->IsBasicDisplayFallback)
    {
        return TRUE;
    }

    (VOID)KeWaitForSingleObject(&g_PostDisplayOwnershipMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);

    Owner = DxgkpReferencePostDisplayOwner(&OwnerDeviceObject);
    RegisterDisplayBridge = (Owner == NULL || Owner == Claimant);
    /*
     * The basic-display fallback is root-enumerated and starts before any PCI
     * display adapter, so it claims the POST framebuffer first every time.  It
     * is meant to hold the desktop only until the real driver for the display
     * arrives, exactly as Windows' basic display driver yields.  dxgkrnl has
     * the whole handover transaction already, but it hangs off
     * DxgkCbAcquirePostDisplayOwnership, and a miniport is not obliged to call
     * that -- igdkmd never does.  Nothing then drives the yield, so the real
     * GPU stays permanently headless behind the fallback.  Claim on its
     * behalf.
     */
    TakeOverFromFallback = !RegisterDisplayBridge &&
                           Owner != NULL &&
                           Owner->MiniportContext != NULL &&
                           Owner->MiniportContext->IsBasicDisplayFallback;

    if (OwnerDeviceObject != NULL)
        ObDereferenceObject(OwnerDeviceObject);
    KeReleaseMutex(&g_PostDisplayOwnershipMutex, FALSE);

    if (TakeOverFromFallback)
    {
        DXGK_DISPLAY_INFORMATION DisplayInformation;
        BOOLEAN Connected;
        NTSTATUS OwnershipStatus;

        /* A successful StartDevice can describe a GPU with no display
         * attached. Keep the working fallback until the claimant has an
         * output to drive; stopping it here otherwise leaves win32k with
         * only a zero-path VidPN and causes VIDEO_DRIVER_INIT_FAILURE. */
        OwnershipStatus = DxgkPnpQueryInitialDisplayConnection(Claimant,
                                                               &Connected);
        if (!NT_SUCCESS(OwnershipStatus) || !Connected)
        {
            DXGKRNL_INFO("DISPLAY_BRIDGE: adapter %p has no confirmed output "
                         "(query=0x%08lX); BasicDisplay retains the desktop\n",
                         Claimant, OwnershipStatus);
            return FALSE;
        }

        RtlZeroMemory(&DisplayInformation, sizeof(DisplayInformation));
        OwnershipStatus = DxgkpAcquirePostDisplayOwnership((HANDLE)Claimant,
                                                           &DisplayInformation,
                                                           NULL);
        DXGKRNL_ERR("DISPLAY_BRIDGE: adapter %p claiming POST ownership from the "
                    "basic-display fallback -> 0x%08lX (%ux%u)\n",
                    Claimant,
                    OwnershipStatus,
                    DisplayInformation.Width,
                    DisplayInformation.Height);

        (VOID)KeWaitForSingleObject(&g_PostDisplayOwnershipMutex,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
        OwnerDeviceObject = NULL;
        Owner = DxgkpReferencePostDisplayOwner(&OwnerDeviceObject);
        RegisterDisplayBridge = (Owner == NULL || Owner == Claimant);
        if (OwnerDeviceObject != NULL)
            ObDereferenceObject(OwnerDeviceObject);
        KeReleaseMutex(&g_PostDisplayOwnershipMutex, FALSE);
    }

    if (!RegisterDisplayBridge)
    {
        DXGKRNL_WARN("DISPLAY_BRIDGE: adapter %p did not claim POST ownership; "
                     "owner %p retains the win32ss desktop bridge\n",
                     Claimant,
                     Owner);
    }
    return RegisterDisplayBridge;
}

/* Complete the claimant-bound handoff after the complete start state has been
 * published. A successful or non-restartable claimant commits the handoff.
 * A restartable failure starts the retained BasicDisplay FDO again while the
 * ownership mutex excludes a second claimant. KMUTEX recursion permits the
 * fallback's AcquirePostDisplayOwnership callback on this same thread. */
static VOID
DxgkpCompletePostDisplayHandoff(
    _In_ PDXGKRNL_ADAPTER Claimant,
    _In_ NTSTATUS StartStatus,
    _In_ BOOLEAN Restartable)
{
    DXGK_POST_DISPLAY_COMPLETION_ACTION Action;
    PDXGKRNL_ADAPTER FallbackAdapter;
    PDEVICE_OBJECT FallbackDeviceObject;
    PDEVICE_OBJECT OwnerDeviceObject;
    PDXGKRNL_ADAPTER Owner;
    BOOLEAN FallbackRemoveRundownHeld;
    BOOLEAN SuppressFallbackUi;
    NTSTATUS RollbackStatus;

    (VOID)KeWaitForSingleObject(&g_PostDisplayOwnershipMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    Action = DxgkPostDisplayCoreComplete(&Claimant->PostDisplayHandoffCore,
                                         StartStatus,
                                         Restartable);
    if (Action == DxgkPostDisplayCompletionNone)
    {
        KeReleaseMutex(&g_PostDisplayOwnershipMutex, FALSE);
        return;
    }

    FallbackAdapter = Claimant->PostDisplayFallbackAdapter;
    FallbackDeviceObject = Claimant->PostDisplayFallbackDeviceObject;
    FallbackRemoveRundownHeld = Claimant->PostDisplayFallbackRemoveRundownHeld;
    Claimant->PostDisplayFallbackAdapter = NULL;
    Claimant->PostDisplayFallbackDeviceObject = NULL;
    Claimant->PostDisplayFallbackRemoveRundownHeld = FALSE;
    ASSERT(FallbackAdapter != NULL);
    ASSERT(FallbackDeviceObject != NULL);
    ASSERT(FallbackRemoveRundownHeld);
    SuppressFallbackUi = (Action == DxgkPostDisplayCompletionCommit);

    if (Action == DxgkPostDisplayCompletionRollback && FallbackAdapter != NULL)
    {
        Owner = DxgkpReferencePostDisplayOwner(&OwnerDeviceObject);
        if (Owner == NULL)
        {
            DxgkpSetBasicDisplayUiSuppressed(FallbackAdapter, FALSE);
            DXGKRNL_WARN("POSTDISPLAY_ROLLBACK: claimant %p failed "
                         "0x%08lX; restarting BasicDisplay adapter %p\n",
                         Claimant,
                         StartStatus,
                         FallbackAdapter);
            RollbackStatus = DxgkAdapterStart(FallbackAdapter, NULL, NULL);
            if (NT_SUCCESS(RollbackStatus))
                DXGKRNL_WARN("POSTDISPLAY_ROLLBACK: BasicDisplay adapter %p "
                             "restored GOP display ownership\n",
                             FallbackAdapter);
            else
                DXGKRNL_ERR("POSTDISPLAY_ROLLBACK: BasicDisplay adapter %p "
                            "restart failed 0x%08lX\n",
                            FallbackAdapter,
                            RollbackStatus);
        }
        else
        {
            SuppressFallbackUi = TRUE;
            DXGKRNL_WARN("POSTDISPLAY_ROLLBACK: claimant %p failed but "
                         "adapter %p already owns the boot display; "
                         "fallback remains stopped\n",
                         Claimant,
                         Owner);
        }
        if (OwnerDeviceObject != NULL)
            ObDereferenceObject(OwnerDeviceObject);
    }

    if (FallbackAdapter != NULL)
        DxgkpSetBasicDisplayUiSuppressed(FallbackAdapter, SuppressFallbackUi);

    KeReleaseMutex(&g_PostDisplayOwnershipMutex, FALSE);
    if (FallbackRemoveRundownHeld && FallbackAdapter != NULL)
        ExReleaseRundownProtection(&FallbackAdapter->RemoveRundownRef);
    if (FallbackDeviceObject != NULL)
        ObDereferenceObject(FallbackDeviceObject);
}

static NTSTATUS
DxgkpRemoveMiniportDevice(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PVOID MiniportDeviceContext)
{
    NTSTATUS Status;

    if (Adapter == NULL || Adapter->MiniportContext == NULL || Adapter->MiniportContext->InitData.s.DxgkDdiRemoveDevice == NULL || MiniportDeviceContext == NULL)
        return STATUS_NOT_SUPPORTED;
    _SEH2_TRY
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiRemoveDevice(MiniportDeviceContext);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

/* ========================================================================
 * Bugcheck-time display
 *
 * Windows brings the panic screen up through the display owner's
 * DxgkDdiSystemDisplayEnable.  The documented plumbing available to a
 * driver is KeRegisterBugCheckCallback: at bugcheck time the callback
 * tells the owning miniport to fall back to a kernel-writable linear
 * frame buffer (rpi5vc4 re-points the HVS at the firmware framebuffer),
 * after which the Inbv-driven bugcheck output is actually visible.
 * ====================================================================== */

static KBUGCHECK_CALLBACK_RECORD g_DxgkBugCheckRecord;

static VOID
NTAPI
DxgkpBugCheckCallback(
    _In_opt_ PVOID Buffer,
    _In_ ULONG Length)
{
    PDXGKRNL_ADAPTER Owner;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE PfnEnable;
    DXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags;
    UINT Width = 0;
    UINT Height = 0;
    D3DDDIFORMAT ColorFormat = D3DDDIFMT_UNKNOWN;
    KIRQL CurrentIrql;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN LockAtDpcLevel;

    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);

    CurrentIrql = KeGetCurrentIrql();
    LockAtDpcLevel = (CurrentIrql >= DISPATCH_LEVEL);
    if (LockAtDpcLevel)
    {
        if (!KeTryToAcquireSpinLockAtDpcLevel(&g_PostDisplayOwnerLock))
            return;
    }
    else
    {
        KeAcquireSpinLock(&g_PostDisplayOwnerLock, &OldIrql);
    }

    Owner = g_PostDisplayOwnerAdapter;
    if (Owner == NULL || Owner->State != DxgkAdapterStateStarted || Owner->MiniportContext == NULL)
        goto ReleaseLock;

    PfnEnable = DXGK_CB(Owner, DxgkDdiSystemDisplayEnable);
    if (PfnEnable == NULL)
        goto ReleaseLock;

    RtlZeroMemory(&Flags, sizeof(Flags));
    (VOID)PfnEnable(Owner->MiniportDeviceContext, 0, &Flags, &Width, &Height, &ColorFormat);

ReleaseLock:
    if (LockAtDpcLevel)
        KeReleaseSpinLockFromDpcLevel(&g_PostDisplayOwnerLock);
    else
        KeReleaseSpinLock(&g_PostDisplayOwnerLock, OldIrql);
}

VOID
DxgkpRegisterBugCheckCallback(VOID)
{
    KeInitializeCallbackRecord(&g_DxgkBugCheckRecord);
    KeRegisterBugCheckCallback(&g_DxgkBugCheckRecord,
                               DxgkpBugCheckCallback,
                               NULL,
                               0,
                               (PUCHAR)"dxgkrnl");
}

/* ========================================================================
 * TDR watchdog
 *
 * A per-adapter timer watches the oldest tracked submission and measures
 * elapsed time since the last observed fence progress.  A work item performs
 * the documented timeout recovery after the configured TdrDelay:
 * DxgkDdiResetFromTimeout -> DxgkDdiRestartFromTimeout -> retire.
 * ====================================================================== */

#define DXGKP_TDR_TICK_MS 100

static DECLSPEC_NORETURN VOID
DxgkpBugCheckTdrFailure(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ NTSTATUS FailureStatus)
{
    ULONG_PTR OwnerTag = 0;

    DXGKRNL_ERR("DxgkpBugCheckTdrFailure: unrecoverable TDR on adapter %p status 0x%08lX\n", Adapter, FailureStatus);
    if (Adapter->MiniportContext != NULL && DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) != NULL)
        OwnerTag = (ULONG_PTR)DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout);
    KeBugCheckEx(0x116, (ULONG_PTR)Adapter, OwnerTag, (ULONG_PTR)FailureStatus, 0);
}

static VOID
NTAPI
DxgkpTdrDdiDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    if (Adapter != NULL && InterlockedCompareExchange(&Adapter->TdrDdiTimerArmed, 1, 1) != 0)
        DxgkpBugCheckTdrFailure(Adapter, STATUS_IO_TIMEOUT);
}

VOID
DxgkpArmTdrDdiDeadline(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Due;
    ULONGLONG Due100ns = (ULONGLONG)Adapter->TdrConfig.TdrDdiDelay * 10000000ULL;

    if (Due100ns == 0)
        Due100ns = 1;
    if (Due100ns > (ULONGLONG)MAXLONGLONG)
        Due100ns = (ULONGLONG)MAXLONGLONG;
    Due.QuadPart = -(LONGLONG)Due100ns;
    InterlockedExchange(&Adapter->TdrDdiTimerArmed, 1);
    KeSetTimer(&Adapter->TdrDdiTimer, Due, &Adapter->TdrDdiDpc);
}

VOID
DxgkpDisarmTdrDdiDeadline(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    InterlockedExchange(&Adapter->TdrDdiTimerArmed, 0);
    KeCancelTimer(&Adapter->TdrDdiTimer);
    KeRemoveQueueDpc(&Adapter->TdrDdiDpc);
    KeFlushQueuedDpcs();
}

static BOOLEAN
DxgkpTdrRecoveryLimitExhausted(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONGLONG Now100ns)
{
    ULONGLONG Window100ns = (ULONGLONG)Adapter->TdrConfig.TdrLimitTime * 10000000ULL;
    ULONG Count = 0;
    ULONG Index;
    KIRQL OldIrql;

    if (Adapter->TdrConfig.TdrDebugMode == DXGKP_TDR_DEBUG_RECOVER_UNCONDITIONAL)
        return FALSE;
    if (Adapter->TdrConfig.TdrLimitCount == 0)
        return TRUE;
    KeAcquireSpinLock(&Adapter->TdrHistoryLock, &OldIrql);
    for (Index = 0; Index < Adapter->TdrRecoveryEntryCount; ++Index)
    {
        ULONGLONG Timestamp = Adapter->TdrRecoveryTimestamps[Index];

        if (Timestamp != 0 && Now100ns >= Timestamp && Now100ns - Timestamp <= Window100ns)
            Count++;
    }
    KeReleaseSpinLock(&Adapter->TdrHistoryLock, OldIrql);
    return Count >= Adapter->TdrConfig.TdrLimitCount;
}

static VOID
DxgkpRecordTdrRecovery(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONGLONG Timestamp100ns)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->TdrHistoryLock, &OldIrql);
    Adapter->TdrRecoveryTimestamps[Adapter->TdrRecoveryWriteIndex] = Timestamp100ns;
    Adapter->TdrRecoveryWriteIndex = (Adapter->TdrRecoveryWriteIndex + 1) % DXGKP_TDR_HISTORY_CAPACITY;
    if (Adapter->TdrRecoveryEntryCount < DXGKP_TDR_HISTORY_CAPACITY)
        Adapter->TdrRecoveryEntryCount++;
    KeReleaseSpinLock(&Adapter->TdrHistoryLock, OldIrql);
    /* The ring above saturates at its capacity because it answers a rate
     * question.  D3DKMTQueryStatistics(ADAPTER) asks a lifetime one, so the
     * total is counted separately and never wraps back to the window. */
    InterlockedIncrement(&Adapter->TdrDetectedCount);
}

static VOID
NTAPI
DxgkpTdrWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;
    ULONG WorkFence;
    ULONG WorkNode;
    ULONG WorkEngine;
    ULONG NotifyType;
    ULONG PreemptionFenceId = 0;
    ULONG CompletedBeforePreempt;
    ULONG CompletedAfterPreempt;
    BOOLEAN AdapterStartedAfterReset;
    BOOLEAN DdiDeadlineArmed = FALSE;
    BOOLEAN Level3Transition = FALSE;
    BOOLEAN PresentResetStarted = FALSE;
    BOOLEAN SchedulerPrepared = FALSE;
    NTSTATUS Status;

    if (Adapter == NULL)
        return;

    DxgkpArmTdrDdiDeadline(Adapter);
    DdiDeadlineArmed = TRUE;
    DxgkAcquireLevel3Transition(Adapter);
    Level3Transition = TRUE;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0)
        goto Exit;
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_OFF)
        goto Exit;
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_BUGCHECK)
        DxgkpBugCheckTdrFailure(Adapter, STATUS_IO_TIMEOUT);
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_RECOVER_VGA)
        DxgkpBugCheckTdrFailure(Adapter, STATUS_NOT_SUPPORTED);
    if (DxgkpTdrRecoveryLimitExhausted(Adapter, KeQueryInterruptTime()))
        DxgkpBugCheckTdrFailure(Adapter, STATUS_IO_TIMEOUT);
    if (Adapter->TdrConfig.TdrDebugMode == DXGKP_TDR_DEBUG_BREAK && !KD_DEBUGGER_NOT_PRESENT)
        DbgBreakPoint();

    WorkFence = Adapter->TdrWorkFence;
    WorkNode = Adapter->TdrWorkNode;
    WorkEngine = Adapter->TdrWorkEngine;

    DXGKRNL_ERR("DxgkpTdrWorker: GPU timeout — fence %lu stuck on adapter %p "
                "irq=%ld queue=%ld dpc=%ld last-dma=%ld\n",
                WorkFence,
                Adapter,
                Adapter->InterruptCount,
                Adapter->QueueDpcCount,
                Adapter->DpcCount,
                Adapter->LastDmaCompletedFence);
    if (Adapter->NotifyInterruptTypeCount[DXGK_INTERRUPT_DMA_PAGE_FAULTED] != 0)
    {
        DXGKRNL_ERR("DxgkpTdrWorker: page-fault={fence=%ld seq=0x%I64x "
                    "stage=%ld bind=%ld flags=0x%lx va=0x%I64x node=%ld "
                    "engine=%ld level=%ld error=0x%08lx process=0x%I64x}\n",
                    Adapter->LastPageFaultFence,
                    Adapter->LastPageFaultPrimitiveSequence,
                    Adapter->LastPageFaultPipelineStage,
                    Adapter->LastPageFaultBindTableEntry,
                    Adapter->LastPageFaultFlags,
                    Adapter->LastPageFaultVirtualAddress,
                    Adapter->LastPageFaultNode,
                    Adapter->LastPageFaultEngine,
                    Adapter->LastPageFaultLevel,
                    Adapter->LastPageFaultErrorCode,
                    Adapter->LastPageFaultProcessHandle);
    }
    for (NotifyType = 0;
         NotifyType < RTL_NUMBER_OF(Adapter->NotifyInterruptTypeCount);
         NotifyType++)
    {
        LONG NotifyCount = Adapter->NotifyInterruptTypeCount[NotifyType];

        if (NotifyCount != 0)
        {
            DXGKRNL_ERR("DxgkpTdrWorker: notify-type=%lu count=%ld\n",
                        NotifyType,
                        NotifyCount);
        }
    }
    {
        ULONGLONG Now100ns = DxgkDiagNow100ns();

        DXGKRNL_ERR("DxgkpTdrWorker: last-isr=-%I64dus last-dma-notify=-%I64dus other-isr=%ld last-other-isr=-%I64dus (msg %ld) unhandled-isr=%ld\n",
                    Adapter->LastIsrTime100ns != 0 ? (LONGLONG)(Now100ns - (ULONGLONG)Adapter->LastIsrTime100ns) / 10 : -1,
                    Adapter->LastDmaNotifyTime100ns != 0 ? (LONGLONG)(Now100ns - (ULONGLONG)Adapter->LastDmaNotifyTime100ns) / 10 : -1,
                    Adapter->OtherIsrCount,
                    Adapter->LastOtherIsrTime100ns != 0 ? (LONGLONG)(Now100ns - (ULONGLONG)Adapter->LastOtherIsrTime100ns) / 10 : -1,
                    Adapter->LastOtherIsrMessage,
                    Adapter->UnhandledIsrCount);
    }
    {
        ULONGLONG Now100ns = DxgkDiagNow100ns();

        DXGKRNL_ERR("DxgkpTdrWorker: runtime-pm caps=%u callbacks active=%ld idle=%ld latency=%ld residency=%ld control=%ld fstate-complete=%ld last-active=comp %ld -%I64dus last-idle=comp %ld -%I64dus\n",
                    0xFFu,
                    Adapter->PowerActiveCalls, Adapter->PowerIdleCalls, Adapter->PowerLatencyCalls,
                    Adapter->PowerResidencyCalls, Adapter->PowerControlRequestCalls, Adapter->PowerFStateCompleteCalls,
                    Adapter->PowerLastActiveComponent,
                    Adapter->PowerLastActiveTime100ns != 0 ? (LONGLONG)(Now100ns - (ULONGLONG)Adapter->PowerLastActiveTime100ns) / 10 : -1,
                    Adapter->PowerLastIdleComponent,
                    Adapter->PowerLastIdleTime100ns != 0 ? (LONGLONG)(Now100ns - (ULONGLONG)Adapter->PowerLastIdleTime100ns) / 10 : -1);
    }
    /* Attempt engine preemption first and give the miniport a short window to
     * report DMA_PREEMPTED progress. A preempted but incomplete packet is not
     * completion: until resubmission exists, it must continue into TDR reset. */
    if (WorkNode >= Adapter->NodeCount || WorkNode >= DXGK_MAX_TRACKED_NODES)
        DxgkpBugCheckTdrFailure(Adapter, STATUS_INVALID_PARAMETER);
    if (!DxgkIsSubmittedFenceIdentity(Adapter, WorkNode, WorkFence))
        goto Exit;
    CompletedBeforePreempt = Adapter->NodeLastCompletedFenceId[WorkNode];
    if ((LONG)(CompletedBeforePreempt - WorkFence) >= 0)
    {
        Adapter->TdrStuckTicks = 0;
        goto Exit;
    }
    Status = VidSchPreemptEngine(Adapter, WorkNode, WorkEngine, &PreemptionFenceId);
    if (NT_SUCCESS(Status))
    {
        if (PreemptionFenceId != 0)
            (VOID)VidSchWaitForPreemption(Adapter, WorkNode, WorkEngine, PreemptionFenceId, 100);
        CompletedAfterPreempt = Adapter->NodeLastCompletedFenceId[WorkNode];
        if ((LONG)(CompletedAfterPreempt - WorkFence) >= 0)
        {
            DXGKRNL_ERR("DxgkpTdrWorker: preemption recovered adapter %p (fence %lu -> %lu), skipping reset\n", Adapter, CompletedBeforePreempt, CompletedAfterPreempt);
            Adapter->TdrStuckTicks = 0;
            goto Exit;
        }
    }

    DXGKRNL_ERR("DxgkpTdrWorker: preemption did not recover — resetting "
                "adapter %p\n", Adapter);

    /* Attribute the hang before the reset destroys the evidence: the oldest
     * active packet of each engine with its submit-time batch head, and the
     * recent GPU VA operations (the fault path prints the same). */
    VidSchDumpEngineDiagnostics(Adapter);
    DxgkGpuVaDumpRecentEvents();
    DxgkVidMmDumpApertureOps();
    DxgkPagingDumpRecentOps();
    DxgkVidMmDumpContextImages(NULL, NULL, "tdr");

    DxgkPresentBeginReset(Adapter);
    PresentResetStarted = TRUE;
    Status = VidSchPrepareAdapterReset(Adapter);
    if (NT_SUCCESS(Status))
        SchedulerPrepared = TRUE;
    else if (Status != STATUS_NOT_SUPPORTED)
    {
        DXGKRNL_ERR("DxgkpTdrWorker: scheduler reset preparation failed 0x%08lX\n", Status);
        DxgkpBugCheckTdrFailure(Adapter, Status);
    }

    DxgkBeginKmdExclusive(Adapter);
    DxgkVidMmQuiesceAdapter(Adapter);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        if (SchedulerPrepared)
            VidSchCompleteAdapterReset(Adapter, FALSE);
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0 || Adapter->MiniportDeviceContext == NULL)
    {
        DxgkReleaseMiniportCallback(Adapter);
        if (SchedulerPrepared)
            VidSchCompleteAdapterReset(Adapter, FALSE);
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }

    if (DXGK_CB(Adapter, DxgkDdiCollectDbgInfo) != NULL)
    {
        DXGKARG_COLLECTDBGINFO CollectArgs;
        UCHAR DbgBuffer[256];

        RtlZeroMemory(&CollectArgs, sizeof(CollectArgs));
        RtlZeroMemory(DbgBuffer, sizeof(DbgBuffer));
        CollectArgs.Reason = 0x117;     /* VIDEO_TDR_TIMEOUT_DETECTED */
        CollectArgs.pBuffer = DbgBuffer;
        CollectArgs.BufferSize = sizeof(DbgBuffer);

        _SEH2_TRY
        {
            (VOID)DXGK_CB(Adapter, DxgkDdiCollectDbgInfo)(Adapter->MiniportDeviceContext, &CollectArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    Status = STATUS_NOT_SUPPORTED;
    if (DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) != NULL)
    {
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout)(Adapter->MiniportDeviceContext);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

    }
    DxgkReleaseMiniportCallback(Adapter);

    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);
    InterlockedExchange(&Adapter->TdrCompletionNotificationsEnabled, 0);
    DxgkDrainVidSchCallbacks(Adapter);
    AdapterStartedAfterReset = Adapter->State == DxgkAdapterStateStarted;
    DxgkTdrResetAdapterSynchronizationObjects(Adapter);
    Status = DxgkVidMmRecoverFromTimeout(Adapter);
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    if (SchedulerPrepared)
        VidSchCompleteAdapterReset(Adapter, TRUE);
    DxgkReleaseTrackedDmaBuffers(Adapter, TRUE);
    DxgkResetSubmittedFenceIdentities(Adapter);
    if (!AdapterStartedAfterReset || Adapter->State != DxgkAdapterStateStarted)
    {
        Adapter->TdrStuckTicks = 0;
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }

    Status = STATUS_NOT_SUPPORTED;
    if (DxgkAcquireMiniportCallback(Adapter))
    {
        if (Adapter->State == DxgkAdapterStateStarted && DXGK_CB_FULL(Adapter, DxgkDdiRestartFromTimeout) != NULL)
        {
            _SEH2_TRY
            {
                Status = DXGK_CB_FULL(Adapter, DxgkDdiRestartFromTimeout)(Adapter->MiniportDeviceContext);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        DxgkReleaseMiniportCallback(Adapter);
    }
    if (Adapter->State != DxgkAdapterStateStarted)
    {
        Adapter->TdrStuckTicks = 0;
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckTdrFailure(Adapter, Status);

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->State != DxgkAdapterStateStarted || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        Adapter->TdrStuckTicks = 0;
        DxgkEndKmdExclusive(Adapter, FALSE);
        goto Exit;
    }
    DxgkVidMmResumeAdapter(Adapter);
    if (SchedulerPrepared)
    {
        Status = VidSchResumeScheduler(Adapter);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
            DxgkpBugCheckTdrFailure(Adapter, Status);
        }
    }
    Adapter->TdrStuckTicks = 0;
    DxgkpRecordTdrRecovery(Adapter, KeQueryInterruptTime());
    InterlockedExchange(&Adapter->TdrCompletionNotificationsEnabled, 1);
    DxgkEndKmdExclusive(Adapter, TRUE);
    DxgkPresentCompleteReset(Adapter);
    PresentResetStarted = FALSE;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    /* The miniport's allocation state did not survive the reset: every open
     * of CDD's shared shadow and primary failed afterwards with
     * STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE and the desktop never
     * presented again (2026-09-06).  Windows re-commits the VidPN after a
     * TDR so the display owner recreates its surfaces; do the same, which
     * recreates the shared surfaces under the shared-surface mutation and
     * lets the Present bindings rebuild on them. */
    {
        NTSTATUS RecommitStatus = DxgkDisplayCommitVidPn(Adapter);

        if (!NT_SUCCESS(RecommitStatus))
            DXGKRNL_ERR("DxgkpTdrWorker: VidPN recommit after reset failed 0x%08lX\n", RecommitStatus);
        else
            DXGKRNL_WARN("DxgkpTdrWorker: VidPN recommitted after reset; shared surfaces recreated\n");
    }

Exit:
    if (PresentResetStarted)
        DxgkPresentCompleteReset(Adapter);
    if (DdiDeadlineArmed)
        DxgkpDisarmTdrDdiDeadline(Adapter);
    ExReleaseRundownProtection(&Adapter->RundownRef);
    InterlockedExchange(&Adapter->TdrWorkQueued, 0);
    if (Level3Transition)
        DxgkReleaseLevel3Transition(Adapter);
}

static VOID
NTAPI
DxgkpTdrDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)DeferredContext;
    ULONG HeadFence = 0;
    ULONG HeadNode = 0;
    ULONG HeadEngine = 0;
    ULONG CompletedFence = 0;
    ULONG SchedulerFence = 0;
    ULONG SchedulerNode = 0;
    ULONG SchedulerEngine = 0;
    ULONGLONG Delay100ns;
    ULONGLONG Now100ns;
    BOOLEAN Outstanding = FALSE;
    BOOLEAN SchedulerOutstanding;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0)
        return;
    Now100ns = KeQueryInterruptTime();
    DxgkKmtReportStuckIoctls(Now100ns);

    KeAcquireSpinLockAtDpcLevel(&Adapter->SubmitDmaLock);
    if (!IsListEmpty(&Adapter->SubmitDmaListHead))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Head = CONTAINING_RECORD(Adapter->SubmitDmaListHead.Flink, DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
        HeadFence = Head->SubmissionFenceId;
        HeadNode = Head->NodeOrdinal;
        HeadEngine = Head->EngineOrdinal;
        Outstanding = TRUE;
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->SubmitDmaLock);

    SchedulerOutstanding = VidSchGetOldestKickedPacket(Adapter, &SchedulerFence, &SchedulerNode, &SchedulerEngine);
    if (SchedulerOutstanding && (!Outstanding || (LONG)(SchedulerFence - HeadFence) < 0))
    {
        HeadFence = SchedulerFence;
        HeadNode = SchedulerNode;
        HeadEngine = SchedulerEngine;
        Outstanding = TRUE;
    }

    if (Outstanding)
    {
        if (HeadNode >= Adapter->NodeCount || HeadNode >= DXGK_MAX_TRACKED_NODES)
            return;
        CompletedFence = Adapter->NodeLastCompletedFenceId[HeadNode];
    }
    if (!Outstanding || (LONG)(CompletedFence - HeadFence) >= 0)
    {
        /* Idle, or completed but not yet retired: not stuck. */
        Adapter->TdrStuckTicks = 0;
        Adapter->TdrLastObservedFence = HeadFence;
        Adapter->TdrLastObservedNode = HeadNode;
        Adapter->TdrLastObservedEngine = HeadEngine;
        Adapter->TdrLastObservedCompletedFence = CompletedFence;
        Adapter->TdrLastProgressTime100ns = Now100ns;
        return;
    }

    if (HeadFence != Adapter->TdrLastObservedFence || HeadNode != Adapter->TdrLastObservedNode || HeadEngine != Adapter->TdrLastObservedEngine || CompletedFence != Adapter->TdrLastObservedCompletedFence)
    {
        Adapter->TdrLastObservedFence = HeadFence;
        Adapter->TdrLastObservedNode = HeadNode;
        Adapter->TdrLastObservedEngine = HeadEngine;
        Adapter->TdrLastObservedCompletedFence = CompletedFence;
        Adapter->TdrStuckTicks = 0;
        Adapter->TdrLastProgressTime100ns = Now100ns;
        return;
    }

    Delay100ns = (ULONGLONG)Adapter->TdrConfig.TdrDelay * 10000000ULL;
    if (Now100ns < Adapter->TdrLastProgressTime100ns || Now100ns - Adapter->TdrLastProgressTime100ns < Delay100ns)
        return;
    Adapter->TdrLastProgressTime100ns = Now100ns;
    if (Adapter->TdrConfig.TdrDebugMode == DXGKP_TDR_DEBUG_IGNORE_TIMEOUT)
        return;

    {
        Adapter->TdrStuckTicks = 0;
        if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || !ExAcquireRundownProtection(&Adapter->RundownRef))
            return;
        if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0)
        {
            ExReleaseRundownProtection(&Adapter->RundownRef);
            return;
        }
        if (InterlockedCompareExchange(&Adapter->TdrWorkQueued, 1, 0) == 0)
        {
            if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->TdrTimerActive, 0, 0) == 0)
            {
                InterlockedExchange(&Adapter->TdrWorkQueued, 0);
                ExReleaseRundownProtection(&Adapter->RundownRef);
                return;
            }
            Adapter->TdrWorkFence = HeadFence;
            Adapter->TdrWorkNode = HeadNode;
            Adapter->TdrWorkEngine = HeadEngine;
            KeMemoryBarrier();
            ExQueueWorkItem(&Adapter->TdrWorkItem, DelayedWorkQueue);
        }
        else
        {
            ExReleaseRundownProtection(&Adapter->RundownRef);
        }
    }
}

static VOID
DxgkpStartTdrWatchdog(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Due;

    KeInitializeTimer(&Adapter->TdrTimer);
    KeInitializeDpc(&Adapter->TdrDpc, DxgkpTdrDpcRoutine, Adapter);
    KeInitializeTimer(&Adapter->TdrDdiTimer);
    KeInitializeDpc(&Adapter->TdrDdiDpc, DxgkpTdrDdiDpcRoutine, Adapter);
    ExInitializeWorkItem(&Adapter->TdrWorkItem, DxgkpTdrWorker, Adapter);
    Adapter->TdrWorkQueued = 0;
    Adapter->TdrOwnershipUncertain = 0;
    Adapter->TdrWorkFence = 0;
    Adapter->TdrWorkNode = 0;
    Adapter->TdrWorkEngine = 0;
    Adapter->TdrLastObservedFence = 0;
    Adapter->TdrLastObservedNode = 0;
    Adapter->TdrLastObservedEngine = 0;
    Adapter->TdrLastObservedCompletedFence = 0;
    Adapter->TdrStuckTicks = 0;
    Adapter->TdrLastProgressTime100ns = KeQueryInterruptTime();
    Adapter->TdrDdiTimerArmed = 0;
    DxgkResetSubmittedFenceIdentities(Adapter);
    InterlockedExchange(&Adapter->TdrCompletionNotificationsEnabled, 1);
    if (Adapter->TdrConfig.TdrLevel == DXGKP_TDR_LEVEL_OFF)
    {
        InterlockedExchange(&Adapter->TdrTimerActive, 0);
        return;
    }
    InterlockedExchange(&Adapter->TdrTimerActive, 1);

    Due.QuadPart = -10000LL * DXGKP_TDR_TICK_MS;
    KeSetTimerEx(&Adapter->TdrTimer, Due, DXGKP_TDR_TICK_MS, &Adapter->TdrDpc);
}

static VOID
DxgkpStopTdrWatchdog(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (InterlockedExchange(&Adapter->TdrTimerActive, 0) != 0)
    {
        KeCancelTimer(&Adapter->TdrTimer);
        KeRemoveQueueDpc(&Adapter->TdrDpc);
        KeFlushQueuedDpcs();
    }

    /* A TDR work item may be mid-reset against the miniport; wait it out. */
    DxgkpWaitForFlagClear(&Adapter->TdrWorkQueued);
}

/* ========================================================================
 * Module-local state
 * ====================================================================== */

/*
 * DxgkpInitialized
 *
 * One-time init guard.  0 = not yet initialised, 1 = initialising,
 * 2 = initialised, 3 = initialization failed.  Contending callers wait until
 * the first caller publishes either the complete global state or its failure.
 */
static LONG DxgkpInitialized = 0;
static NTSTATUS DxgkpInitializationStatus = STATUS_UNSUCCESSFUL;
static FAST_MUTEX DxgkpMapMemoryMutex;
static LIST_ENTRY DxgkpMapMemoryList;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
static FAST_MUTEX DxgkpCallbackMemoryMutex;
static LIST_ENTRY DxgkpCallbackMemoryList;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
static FAST_MUTEX DxgkpPhysicalMemoryMutex;
static LIST_ENTRY DxgkpPhysicalMemoryList;
#endif
static volatile LONG DxgkpTrackedRefreshTraceCount = 0;
static volatile LONG DxgkpRetireTraceCount = 0;
static volatile LONG DxgkpTrackedSampleTraceCount = 0;

#define DXGK_TRACE_SLOW_CONFIG_ACCESS_US   1000ULL
#define DXGK_TRACE_SLOW_SYNC_US            1000ULL
#define DXGK_TRACE_SLOW_DPC_US             1000ULL
#define DXGK_TRACE_ISR_LOG_LIMIT           16
#define DXGK_TRACE_DPC_LOG_LIMIT           16
#define DXGKP_FIELD_END(Type, Field) \
    (FIELD_OFFSET(Type, Field) + sizeof(((Type *)0)->Field))

typedef enum _DXGK_MAPMEM_KIND
{
    DxgkMapMemoryMdl,
    DxgkMapMemoryIoSpace,
    DxgkMapMemoryPortSpace
} DXGK_MAPMEM_KIND;

typedef struct _DXGK_MAPMEM_ENTRY
{
    LIST_ENTRY       ListEntry;
    PDXGKRNL_ADAPTER Adapter;
    PEPROCESS        Process;
    PVOID            VirtualAddress;
    PVOID            BaseAddress;
    PMDL             Mdl;
    PHYSICAL_ADDRESS PhysicalAddress;
    SIZE_T           Length;
    PCSTR            MapMethod;
    DXGK_MAPMEM_KIND Kind;
} DXGK_MAPMEM_ENTRY, *PDXGK_MAPMEM_ENTRY;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
typedef enum _DXGKP_CALLBACK_MEMORY_KIND
{
    DxgkpCallbackMemoryContiguous,
    DxgkpCallbackMemoryMdl,
    DxgkpCallbackMemoryContiguousMdl
} DXGKP_CALLBACK_MEMORY_KIND;

typedef struct _DXGKP_CALLBACK_MEMORY_ENTRY
{
    LIST_ENTRY ListEntry;
    PDXGKRNL_ADAPTER Adapter;
    DXGKP_CALLBACK_MEMORY_KIND Kind;
    union
    {
        PVOID ContiguousMemory;
        PMDL Mdl;
        struct
        {
            PVOID BaseAddress;
            PMDL Mdl;
        } ContiguousMdl;
    } Memory;
} DXGKP_CALLBACK_MEMORY_ENTRY, *PDXGKP_CALLBACK_MEMORY_ENTRY;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
typedef enum _DXGKP_PHYSICAL_MEMORY_BACKING
{
    DxgkpPhysicalBackingMdl,
    DxgkpPhysicalBackingContiguousMdl,
    DxgkpPhysicalBackingContiguous,
    DxgkpPhysicalBackingIoSpace
} DXGKP_PHYSICAL_MEMORY_BACKING;

typedef enum _DXGKP_PHYSICAL_MAPPING_KIND
{
    DxgkpPhysicalMappingDirect,
    DxgkpPhysicalMappingMdl,
    DxgkpPhysicalMappingIoSpace
} DXGKP_PHYSICAL_MAPPING_KIND;

typedef struct _DXGKP_PHYSICAL_MEMORY_OBJECT
{
    LIST_ENTRY ListEntry;
    LIST_ENTRY AdapterMemoryList;
    LIST_ENTRY MappingList;
    LIST_ENTRY AdlList;
    PDXGKRNL_ADAPTER CreatorAdapter;
    DXGK_PHYSICAL_MEMORY_TYPE Type;
    DXGK_MEMORY_CACHING_TYPE CacheType;
    DXGKP_PHYSICAL_MEMORY_BACKING Backing;
    SIZE_T Size;
    ULONG_PTR Context;
    PMDL Mdl;
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS IoBaseAddress;
} DXGKP_PHYSICAL_MEMORY_OBJECT, *PDXGKP_PHYSICAL_MEMORY_OBJECT;

typedef struct _DXGKP_ADAPTER_MEMORY_OBJECT
{
    LIST_ENTRY ListEntry;
    PDXGKP_PHYSICAL_MEMORY_OBJECT PhysicalObject;
    PDXGKRNL_ADAPTER Adapter;
} DXGKP_ADAPTER_MEMORY_OBJECT, *PDXGKP_ADAPTER_MEMORY_OBJECT;

typedef struct _DXGKP_PHYSICAL_MAPPING
{
    LIST_ENTRY ListEntry;
    DXGKP_PHYSICAL_MAPPING_KIND Kind;
    PEPROCESS Process;
    PVOID BaseAddress;
    SIZE_T Size;
    PMDL MappingMdl;
} DXGKP_PHYSICAL_MAPPING, *PDXGKP_PHYSICAL_MAPPING;

typedef struct _DXGKP_ADL_ENTRY
{
    LIST_ENTRY ListEntry;
    PDXGKP_ADAPTER_MEMORY_OBJECT AdapterMemoryObject;
    DXGK_ADL Adl;
} DXGKP_ADL_ENTRY, *PDXGKP_ADL_ENTRY;
#endif

/*
 * Timing for the traces below.
 *
 * KeQueryInterruptTime only advances on a clock interrupt -- a 1 ms tick here --
 * so anything shorter than that measures as either zero or exactly one tick.
 * Used for short operations it does not report a duration at all: it reports
 * whether a tick happened to land inside the call.  That is how every one of
 * six "slow config read" warnings came out at exactly 1000 us with no spread,
 * which was read as a millisecond-long PCI read and is nothing of the kind.
 *
 * The performance counter runs off the ARM64 generic timer and has resolution
 * far below a microsecond, so a duration measured with it is a duration.
 */
FORCEINLINE ULONGLONG
DxgkpTraceNow100ns(VOID)
{
    LARGE_INTEGER Counter;

    Counter = KeQueryPerformanceCounter(NULL);
    return (ULONGLONG)Counter.QuadPart;
}

FORCEINLINE ULONGLONG
DxgkpTraceElapsedUs(
    _In_ ULONGLONG StartTicks)
{
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Counter;
    ULONGLONG EndTicks;

    Counter = KeQueryPerformanceCounter(&Frequency);
    EndTicks = (ULONGLONG)Counter.QuadPart;

    if (EndTicks <= StartTicks || Frequency.QuadPart <= 0)
        return 0;

    return ((EndTicks - StartTicks) * 1000000ULL) / (ULONGLONG)Frequency.QuadPart;
}

FORCEINLINE ULONGLONG
DxgkpTraceSinceStartUs(
    _In_opt_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || Adapter->InterruptTraceEpoch100ns == 0)
        return 0;

    return DxgkpTraceElapsedUs(Adapter->InterruptTraceEpoch100ns);
}

FORCEINLINE BOOLEAN
DxgkpFenceIdReached(
    _In_ ULONG CompletedFenceId,
    _In_ ULONG SubmissionFenceId)
{
    return ((LONG)(CompletedFenceId - SubmissionFenceId) >= 0);
}

static VOID
DxgkpUpdateCompletedFence(
    _Inout_ volatile ULONG *Fence,
    _In_ ULONG CompletedFence)
{
    LONG Current;

    for (;;)
    {
        Current = InterlockedCompareExchange((volatile LONG *)Fence, 0, 0);
        if (DxgkpFenceIdReached((ULONG)Current, CompletedFence) || InterlockedCompareExchange((volatile LONG *)Fence, (LONG)CompletedFence, Current) == Current)
            return;
    }
}

static VOID
DxgkpReleasePostDisplayMapping(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    SIZE_T FbSize;

    if (Adapter == NULL)
        return;

    if (Adapter->PostDisplayVirtualAddress != NULL)
    {
        FbSize = Adapter->PostDisplayMappingSize;
        if (FbSize == 0)
        {
            FbSize =
                (SIZE_T)Adapter->PostDisplayPitch *
                Adapter->PostDisplayHeight;
        }
        ASSERT(FbSize != 0);
        if (FbSize != 0)
        {
            MmUnmapIoSpace(Adapter->PostDisplayVirtualAddress, FbSize);
        }
    }

    Adapter->PostDisplayVirtualAddress = NULL;
    Adapter->PostDisplayMappingSize = 0;
    Adapter->PostDisplayPhysicalAddress.QuadPart = 0;
    Adapter->PostDisplayPitch = 0;
    Adapter->PostDisplayWidth = 0;
    Adapter->PostDisplayHeight = 0;
}

static ULONG
DxgkpShadowAllocateSubmissionFenceId(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG CurrentFenceId;
    ULONG NextFenceId;

    if (Adapter == NULL)
        return 0;

    for (;;)
    {
        CurrentFenceId = (ULONG)InterlockedCompareExchange(&Adapter->NextSubmissionFenceId, 0, 0);
        NextFenceId = CurrentFenceId + 1;
        if (NextFenceId == 0)
            NextFenceId = 1;
        if ((ULONG)InterlockedCompareExchange(&Adapter->NextSubmissionFenceId, (LONG)NextFenceId, (LONG)CurrentFenceId) == CurrentFenceId)
            return NextFenceId;
    }
}

static BOOLEAN
DxgkpShadowReserveSubmissionFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LONG64 Identity;
    LONG64 CurrentIdentity;
    LONG64 PreviousIdentity;
    ULONG FirstTombstone;
    ULONG Probe;
    ULONG Slot;
    ULONG StartSlot;
    ULONG TargetSlot;
    LONG64 TargetValue;

    if (Adapter == NULL || NodeOrdinal >= Adapter->NodeCount || NodeOrdinal >= DXGK_MAX_TRACKED_NODES || SubmissionFenceId == 0)
        return FALSE;
    Identity = (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | SubmissionFenceId);
    StartSlot = (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
    for (;;)
    {
        FirstTombstone = DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY;
        for (Probe = 0; Probe < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Probe)
        {
            Slot = (StartSlot + Probe) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
            CurrentIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0, 0);
            if (CurrentIdentity == Identity || CurrentIdentity == (LONG64)((ULONGLONG)Identity | DXGK_SUBMITTED_FENCE_PUBLISHED_BIT))
                return FALSE;
            if (CurrentIdentity == DXGK_SUBMITTED_FENCE_TOMBSTONE && FirstTombstone == DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY)
                FirstTombstone = Slot;
            if (CurrentIdentity == 0)
                break;
        }
        if (Probe == DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY && FirstTombstone == DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY)
            return FALSE;
        TargetSlot = FirstTombstone != DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY ? FirstTombstone : Slot;
        TargetValue = FirstTombstone != DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY ? DXGK_SUBMITTED_FENCE_TOMBSTONE : 0;
        PreviousIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[TargetSlot], Identity, TargetValue);
        if (PreviousIdentity == TargetValue)
            return TRUE;
    }
}

static VOID
DxgkpShadowPublishSubmittedFence(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LONG64 Identity;
    LONG64 PublishedIdentity;
    LONG64 CurrentIdentity;
    ULONG Probe;
    ULONG Slot;
    ULONG StartSlot;

    if (Adapter == NULL || NodeOrdinal >= Adapter->NodeCount || NodeOrdinal >= DXGK_MAX_TRACKED_NODES || SubmissionFenceId == 0)
        KeBugCheckEx(0x119, 0x1, (ULONG_PTR)SubmissionFenceId, (ULONG_PTR)NodeOrdinal, (ULONG_PTR)Adapter);
    Identity = (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | SubmissionFenceId);
    PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGK_SUBMITTED_FENCE_PUBLISHED_BIT);
    StartSlot = (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
    for (Probe = 0; Probe < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Probe)
    {
        Slot = (StartSlot + Probe) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
        CurrentIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0, 0);
        if (CurrentIdentity == PublishedIdentity)
            break;
        if (CurrentIdentity == 0)
        {
            Probe = DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY;
            break;
        }
        if (CurrentIdentity == Identity && InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], PublishedIdentity, Identity) == Identity)
            break;
    }
    if (Probe == DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY)
        KeBugCheckEx(0x119, 0x1, (ULONG_PTR)SubmissionFenceId, (ULONG_PTR)NodeOrdinal, (ULONG_PTR)Adapter);
    DxgkpUpdateCompletedFence(&Adapter->NodeLastSubmittedFenceId[NodeOrdinal], SubmissionFenceId);
}

static BOOLEAN
DxgkpShadowIsSubmittedFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LONG64 Identity;
    LONG64 PublishedIdentity;
    LONG64 CurrentIdentity;
    ULONG Probe;
    ULONG Slot;
    ULONG StartSlot;

    if (Adapter == NULL || NodeOrdinal >= Adapter->NodeCount || NodeOrdinal >= DXGK_MAX_TRACKED_NODES || SubmissionFenceId == 0)
        return FALSE;
    Identity = (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | SubmissionFenceId);
    PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGK_SUBMITTED_FENCE_PUBLISHED_BIT);
    StartSlot = (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
    for (Probe = 0; Probe < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Probe)
    {
        Slot = (StartSlot + Probe) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
        CurrentIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0, 0);
        if (CurrentIdentity == PublishedIdentity)
            return TRUE;
        if (CurrentIdentity == 0)
            return FALSE;
    }
    return FALSE;
}

static BOOLEAN
DxgkpShadowReleaseSubmittedFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LONG64 Identity;
    LONG64 PublishedIdentity;
    LONG64 CurrentIdentity;
    ULONG Probe;
    ULONG Slot;
    ULONG StartSlot;

    if (Adapter == NULL || NodeOrdinal >= DXGK_MAX_TRACKED_NODES || SubmissionFenceId == 0)
        return FALSE;
    Identity = (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | SubmissionFenceId);
    PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGK_SUBMITTED_FENCE_PUBLISHED_BIT);
    StartSlot = (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
    for (Probe = 0; Probe < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Probe)
    {
        Slot = (StartSlot + Probe) & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1);
        CurrentIdentity = InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0, 0);
        if (CurrentIdentity == 0)
            return FALSE;
        if (CurrentIdentity != Identity && CurrentIdentity != PublishedIdentity)
            continue;
        if (InterlockedCompareExchange64(&Adapter->SubmittedFenceIdentities[Slot], DXGK_SUBMITTED_FENCE_TOMBSTONE, CurrentIdentity) == CurrentIdentity)
            return TRUE;
    }
    return FALSE;
}

static VOID
DxgkpShadowResetSubmittedFenceIdentities(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG Slot;

    if (Adapter == NULL)
        return;
    for (Slot = 0; Slot < DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY; ++Slot)
        InterlockedExchange64(&Adapter->SubmittedFenceIdentities[Slot], 0);
}

static DECLSPEC_NORETURN VOID DxgkpBugCheckMms2Timeline(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    KeBugCheckEx(0x119, 0x1, (ULONG_PTR)FenceId, (ULONG_PTR)NodeOrdinal, (ULONG_PTR)Adapter);
}

static BOOLEAN DxgkpAcquireMms2TimelineCall(_In_ PDXGKRNL_ADAPTER Adapter, _Out_ DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 *Timeline)
{
    LONG ActiveCalls;

    if (Adapter == NULL || Timeline == NULL)
        return FALSE;
    ActiveCalls = InterlockedIncrement(&Adapter->Mms2TimelineActiveCalls);
    ASSERT(ActiveCalls > 0);
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Adapter->Mms2TimelineCallsOpen, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0)
    {
        ActiveCalls = InterlockedDecrement(&Adapter->Mms2TimelineActiveCalls);
        ASSERT(ActiveCalls >= 0);
        return FALSE;
    }
    *Timeline = Adapter->Mms2Timeline;
    return TRUE;
}

static VOID DxgkpReleaseMms2TimelineCall(_In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG ActiveCalls = InterlockedDecrement(&Adapter->Mms2TimelineActiveCalls);

    ASSERT(ActiveCalls >= 0);
}

static BOOLEAN DxgkpCloseMms2TimelineCalls(_In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;
    BOOLEAN WasOpen;

    PAGED_CODE();
    WasOpen = InterlockedExchange(&Adapter->Mms2TimelineCallsOpen, 0) != 0;
    KeMemoryBarrier();
    Delay.QuadPart = -10000;
    while (InterlockedCompareExchange(&Adapter->Mms2TimelineActiveCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    KeMemoryBarrier();
    ASSERT(!WasOpen || InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0);
    return WasOpen;
}

static VOID DxgkpPublishMms2TimelineCalls(_In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->Mms2TimelineValid, 1);
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->Mms2TimelineCallsOpen, 1);
}

static VOID DxgkpReopenMms2TimelineCalls(_In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    ASSERT(InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0);
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->Mms2TimelineCallsOpen, 1);
}

ULONG NTAPI DxgkAllocateSubmissionFenceId(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    ULONG ProviderFence;

    if (Adapter == NULL)
        return 0;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
        return InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0 ? DxgkpShadowAllocateSubmissionFenceId(Adapter) : 0;
    ProviderFence = Timeline.AllocateFence(Timeline.TimelineHandle, Timeline.Generation);
    if (ProviderFence == 0)
    {
        DxgkpReleaseMms2TimelineCall(Adapter);
        return 0;
    }
    DxgkpReleaseMms2TimelineCall(Adapter);
    return ProviderFence;
}

BOOLEAN NTAPI DxgkReserveSubmissionFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId,
    _Out_ PULONG FenceIdentityEpoch)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    ULONG Epoch;
    BOOLEAN ProviderReserved;

    if (FenceIdentityEpoch == NULL)
        return FALSE;
    *FenceIdentityEpoch = 0;
    if (Adapter == NULL)
        return FALSE;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
    {
        if (InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0 ||
            InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityResetting, 0, 0) != 0)
        {
            return FALSE;
        }

        Epoch = (ULONG)InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityEpoch, 0, 0);
        if (Epoch == 0 || !DxgkpShadowReserveSubmissionFenceIdentity(Adapter, NodeOrdinal, SubmissionFenceId))
            return FALSE;
        KeMemoryBarrier();
        if (InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityResetting, 0, 0) != 0 ||
            (ULONG)InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityEpoch, 0, 0) != Epoch)
        {
            (VOID)DxgkpShadowReleaseSubmittedFenceIdentity(Adapter, NodeOrdinal, SubmissionFenceId);
            return FALSE;
        }
        *FenceIdentityEpoch = Epoch;
        return TRUE;
    }
    if (InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityResetting, 0, 0) != 0)
    {
        DxgkpReleaseMms2TimelineCall(Adapter);
        return FALSE;
    }
    Epoch = (ULONG)InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityEpoch, 0, 0);
    if (Epoch == 0)
    {
        DxgkpReleaseMms2TimelineCall(Adapter);
        return FALSE;
    }
    ProviderReserved = Timeline.ReserveFence(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, SubmissionFenceId);
    if (ProviderReserved)
        *FenceIdentityEpoch = Epoch;
    DxgkpReleaseMms2TimelineCall(Adapter);
    return ProviderReserved;
}

VOID NTAPI DxgkPublishSubmittedFence(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;

    if (Adapter == NULL)
        return;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
    {
        if (InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0)
            DxgkpShadowPublishSubmittedFence(Adapter, NodeOrdinal, SubmissionFenceId);
        else
            DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
        return;
    }
    if (!Timeline.PublishFence(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, SubmissionFenceId))
        DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
    DxgkpUpdateCompletedFence(&Adapter->NodeLastSubmittedFenceId[NodeOrdinal], SubmissionFenceId);
    DxgkpReleaseMms2TimelineCall(Adapter);
}

BOOLEAN NTAPI DxgkIsSubmittedFenceIdentity(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    BOOLEAN ProviderPublished;

    if (Adapter == NULL)
        return FALSE;
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
        return InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0 ? DxgkpShadowIsSubmittedFenceIdentity(Adapter, NodeOrdinal, SubmissionFenceId) : FALSE;
    ProviderPublished = Timeline.IsFencePublished(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, SubmissionFenceId);
    DxgkpReleaseMms2TimelineCall(Adapter);
    return ProviderPublished;
}

VOID NTAPI DxgkReleaseSubmittedFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId,
    _In_ ULONG FenceIdentityEpoch)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;

    if (Adapter == NULL)
        return;
    if (FenceIdentityEpoch == 0)
        DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
    if ((ULONG)InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityEpoch, 0, 0) != FenceIdentityEpoch ||
        InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityResetting, 0, 0) != 0)
    {
        return;
    }
    if (!DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
    {
        if ((ULONG)InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityEpoch, 0, 0) != FenceIdentityEpoch ||
            InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityResetting, 0, 0) != 0)
        {
            return;
        }
        if (InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0)
            (VOID)DxgkpShadowReleaseSubmittedFenceIdentity(Adapter, NodeOrdinal, SubmissionFenceId);
        else
            DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
        return;
    }
    if (!Timeline.ReleaseFence(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, SubmissionFenceId))
        DxgkpBugCheckMms2Timeline(Adapter, NodeOrdinal, SubmissionFenceId);
    DxgkpReleaseMms2TimelineCall(Adapter);
}

VOID NTAPI DxgkResetSubmittedFenceIdentities(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    BOOLEAN TimelineWasPublished;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL)
        return;
    InterlockedExchange(&Adapter->SubmittedFenceIdentityResetting, 1);
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) == 0)
    {
        DxgkpShadowResetSubmittedFenceIdentities(Adapter);
        if (InterlockedIncrement(&Adapter->SubmittedFenceIdentityEpoch) == 0)
            InterlockedIncrement(&Adapter->SubmittedFenceIdentityEpoch);
        KeMemoryBarrier();
        InterlockedExchange(&Adapter->SubmittedFenceIdentityResetting, 0);
        return;
    }
    TimelineWasPublished = DxgkpCloseMms2TimelineCalls(Adapter);
    if (!TimelineWasPublished)
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    Timeline = Adapter->Mms2Timeline;
    Status = Timeline.ResetFenceIdentities(Timeline.TimelineHandle, Timeline.Generation);
    if (!NT_SUCCESS(Status))
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    DxgkpShadowResetSubmittedFenceIdentities(Adapter);
    if (InterlockedIncrement(&Adapter->SubmittedFenceIdentityEpoch) == 0)
        InterlockedIncrement(&Adapter->SubmittedFenceIdentityEpoch);
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->SubmittedFenceIdentityResetting, 0);
    DxgkpReopenMms2TimelineCalls(Adapter);
}

NTSTATUS NTAPI DxgkNotifySubmissionFenceCompletion(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId, _In_ BOOLEAN Preempted, _Out_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    NTSTATUS Status;

    if (Adapter == NULL || Snapshot == NULL || !DxgkpAcquireMms2TimelineCall(Adapter, &Timeline))
        return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    Snapshot->Size = DXGMMS2_FENCE_SNAPSHOT_V1_SIZE;
    Snapshot->Version = DXGMMS2_SCHEDULER_TIMELINE_VERSION_1;
    Status = Timeline.NotifyFenceCompletion(Timeline.TimelineHandle, Timeline.Generation, NodeOrdinal, FenceId, Preempted ? DXGMMS2_TIMELINE_NOTIFY_PREEMPTED : 0, Snapshot);
    if (!NT_SUCCESS(Status))
    {
        DxgkpReleaseMms2TimelineCall(Adapter);
        return Status;
    }
    if (Snapshot->Size != DXGMMS2_FENCE_SNAPSHOT_V1_SIZE || Snapshot->Version != DXGMMS2_SCHEDULER_TIMELINE_VERSION_1 || Snapshot->Generation != Timeline.Generation || Snapshot->NodeOrdinal != NodeOrdinal)
    {
        DxgkpReleaseMms2TimelineCall(Adapter);
        return STATUS_DATA_ERROR;
    }
    DxgkpUpdateCompletedFence(&Adapter->NodeLastSubmittedFenceId[NodeOrdinal], Snapshot->LastSubmittedFence);
    DxgkpUpdateCompletedFence(&Adapter->NodeLastCompletedFenceId[NodeOrdinal], Snapshot->LastCompletedFence);
    DxgkpUpdateCompletedFence(&Adapter->LastCompletedSubmissionFenceId, Snapshot->GlobalLastCompletedFence);
    DxgkpReleaseMms2TimelineCall(Adapter);
    return STATUS_SUCCESS;
}

static VOID
DxgkpDestroyDmaBuffer(
    _In_ PDXGKRNL_DMA_BUFFER DmaBuffer);

static ULONG
DxgkpDmaBufferCacheCharge(
    _In_ PDXGKRNL_DMA_BUFFER DmaBuffer)
{
    return (ULONG)ROUND_TO_PAGES(DmaBuffer->Capacity);
}

/* Windows PresentFromCdd obtains a retired buffer from VidMm's DMA pool. Keep
 * the same ownership shape here for both physically contiguous and VidMm
 * segment-backed buffers. The exact segment set is part of the pool key: a
 * buffer placed for one context contract must not satisfy a different one. */
static PDXGKRNL_DMA_BUFFER
DxgkpTakeCachedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Capacity,
    _In_ ULONG PrivateDataSize,
    _In_ DXGKRNL_DMA_BACKING_KIND BackingKind,
    _In_ ULONG SegmentSet,
    _In_opt_ PDXGKRNL_DEVICE OwnerDevice)
{
    PDXGKRNL_DMA_BUFFER DmaBuffer = NULL;
    KIRQL OldIrql;
    PLIST_ENTRY Link;

    KeAcquireSpinLock(&Adapter->DmaBufferCacheLock, &OldIrql);
    if (InterlockedCompareExchange(&Adapter->DmaBufferCacheStopping, 0, 0) == 0)
    {
        for (Link = Adapter->DmaBufferCacheListHead.Flink;
             Link != &Adapter->DmaBufferCacheListHead;
             Link = Link->Flink)
        {
            PDXGKRNL_DMA_BUFFER Candidate;

            Candidate = CONTAINING_RECORD(Link,
                                          DXGKRNL_DMA_BUFFER,
                                          CacheListEntry);
            if (Candidate->Capacity != Capacity ||
                Candidate->PrivateDataSize != PrivateDataSize ||
                Candidate->BackingKind != BackingKind ||
                Candidate->SegmentSet != SegmentSet ||
                Candidate->OwnerDevice != OwnerDevice)
            {
                continue;
            }

            RemoveEntryList(&Candidate->CacheListEntry);
            InitializeListHead(&Candidate->CacheListEntry);
            ASSERT(Adapter->DmaBufferCacheCount != 0);
            Adapter->DmaBufferCacheCount--;
            ASSERT(Adapter->DmaBufferCacheBytes >= DxgkpDmaBufferCacheCharge(Candidate));
            Adapter->DmaBufferCacheBytes -= DxgkpDmaBufferCacheCharge(Candidate);
            DmaBuffer = Candidate;
            break;
        }
    }
    KeReleaseSpinLock(&Adapter->DmaBufferCacheLock, OldIrql);

    if (DmaBuffer == NULL)
        return NULL;

    DmaBuffer->SubmissionStartOffset = 0;
    DmaBuffer->SubmissionEndOffset = 0;
    DmaBuffer->PrivateDataUsed = 0;
    if (DmaBuffer->PrivateData != NULL)
        RtlZeroMemory(DmaBuffer->PrivateData, DmaBuffer->PrivateDataSize);
    if (DmaBuffer->BackingKind == DxgkDmaBackingVidMm)
    {
        /* TDR recovery tears down aperture placement for every resident
         * allocation. A buffer retired into the pool just before or during
         * recovery can therefore outlive its old SegmentId/address. Drop it
         * and let the caller acquire a newly placed VidMm backing. */
        if ((DmaBuffer->OwnerDevice != NULL &&
             InterlockedCompareExchange(&DmaBuffer->OwnerDevice->Destroying,
                                        0,
                                        0) != 0) ||
            DmaBuffer->BackingAllocation == NULL ||
            !DmaBuffer->BackingAllocation->Resident ||
            DmaBuffer->BackingAllocation->SegmentId == 0 ||
            DmaBuffer->BackingAllocation->SystemMemory == NULL)
        {
            DxgkpDestroyDmaBuffer(DmaBuffer);
            return NULL;
        }
        DmaBuffer->VirtualAddress = DmaBuffer->BackingAllocation->SystemMemory;
        DmaBuffer->SegmentId = DmaBuffer->BackingAllocation->SegmentId;
        DmaBuffer->SegmentAddress =
            DmaBuffer->BackingAllocation->PhysicalAddress;
    }

    return DmaBuffer;
}

NTSTATUS
NTAPI
DxgkAllocateDmaBufferWithPrivateData(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Capacity,
    _In_ ULONG PrivateDataSize,
    _Out_ PDXGKRNL_DMA_BUFFER *OutDmaBuffer)
{
    PDXGKRNL_DMA_BUFFER DmaBuffer;
    PHYSICAL_ADDRESS LowestAddress;
    PHYSICAL_ADDRESS HighestAddress;
    PHYSICAL_ADDRESS BoundaryAddress;
    NTSTATUS Status;

    if (Adapter == NULL || Capacity == 0 || OutDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutDmaBuffer = NULL;
    DmaBuffer = DxgkpTakeCachedDmaBuffer(
                    Adapter,
                    Capacity,
                    PrivateDataSize,
                    DxgkDmaBackingContiguousMemory,
                    0,
                    NULL);
    if (DmaBuffer != NULL)
    {
        *OutDmaBuffer = DmaBuffer;
        return STATUS_SUCCESS;
    }

    DmaBuffer = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DmaBuffer), TAG_DXGK_SUBMITDMA);
    if (DmaBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(DmaBuffer, sizeof(*DmaBuffer));
    InitializeListHead(&DmaBuffer->CacheListEntry);
    DmaBuffer->OwnerAdapter = Adapter;
    LowestAddress.QuadPart = 0;
    HighestAddress = Adapter->HighestAcceptableAddress;
    if (HighestAddress.QuadPart == 0)
        HighestAddress.QuadPart = (LONGLONG)-1;
    BoundaryAddress.QuadPart = 0;
    DmaBuffer->VirtualAddress = MmAllocateContiguousMemorySpecifyCache(Capacity, LowestAddress, HighestAddress, BoundaryAddress, MmCached);
    if (DmaBuffer->VirtualAddress == NULL)
    {
        ExFreePoolWithTag(DmaBuffer, TAG_DXGK_SUBMITDMA);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DmaBuffer->Capacity = Capacity;
    DmaBuffer->PrivateData = NULL;
    DmaBuffer->PrivateDataSize = PrivateDataSize;
    DmaBuffer->PrivateDataUsed = 0;
    if (PrivateDataSize != 0)
    {
        DmaBuffer->PrivateData = ExAllocatePoolWithTag(NonPagedPool, DmaBuffer->PrivateDataSize, TAG_DXGK_SUBMITDMA);
        if (DmaBuffer->PrivateData == NULL)
        {
            MmFreeContiguousMemorySpecifyCache(DmaBuffer->VirtualAddress, Capacity, MmCached);
            ExFreePoolWithTag(DmaBuffer, TAG_DXGK_SUBMITDMA);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(DmaBuffer->PrivateData, DmaBuffer->PrivateDataSize);
    }
    DmaBuffer->SubmissionStartOffset = 0;
    DmaBuffer->SubmissionEndOffset = Capacity;
    DmaBuffer->SegmentSet = 0;
    DmaBuffer->SegmentId = 0;
    DmaBuffer->SegmentAddress = MmGetPhysicalAddress(DmaBuffer->VirtualAddress);
    DmaBuffer->BackingKind = DxgkDmaBackingContiguousMemory;

    /* Establish a clean baseline before a cached DMA buffer can contain
     * device-written regions that the CPU deliberately never touches.  Later
     * submissions only need to clean their declared CPU-written prefix. */
    Status = DxgkFlushDmaBufferForSubmission(DmaBuffer);
    if (!NT_SUCCESS(Status))
    {
        if (DmaBuffer->PrivateData != NULL)
            ExFreePoolWithTag(DmaBuffer->PrivateData, TAG_DXGK_SUBMITDMA);
        MmFreeContiguousMemory(DmaBuffer->VirtualAddress);
        ExFreePoolWithTag(DmaBuffer, TAG_DXGK_SUBMITDMA);
        return Status;
    }
    DmaBuffer->SubmissionEndOffset = 0;
    *OutDmaBuffer = DmaBuffer;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkAllocateDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Capacity,
    _Out_ PDXGKRNL_DMA_BUFFER *OutDmaBuffer)
{
    return DxgkAllocateDmaBufferWithPrivateData(Adapter, Capacity, 0, OutDmaBuffer);
}

NTSTATUS
NTAPI
DxgkAllocateDmaBufferInSegmentSetWithPrivateData(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Capacity,
    _In_ ULONG SegmentSet,
    _In_ ULONG PrivateDataSize,
    _Out_ PDXGKRNL_DMA_BUFFER *OutDmaBuffer)
{
    PDXGKRNL_DMA_BUFFER Buffer;
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status;

    if (SegmentSet == 0)
    {
        return DxgkAllocateDmaBufferWithPrivateData(Adapter,
                                                    Capacity,
                                                    PrivateDataSize,
                                                    OutDmaBuffer);
    }
    if (Adapter == NULL || Capacity == 0 || OutDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutDmaBuffer = NULL;
    Buffer = DxgkpTakeCachedDmaBuffer(Adapter,
                                      Capacity,
                                      PrivateDataSize,
                                      DxgkDmaBackingVidMm,
                                      SegmentSet,
                                      NULL);
    if (Buffer != NULL)
    {
        *OutDmaBuffer = Buffer;
        return STATUS_SUCCESS;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Buffer), TAG_DXGK_SUBMITDMA);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Buffer, sizeof(*Buffer));
    InitializeListHead(&Buffer->CacheListEntry);
    if (PrivateDataSize != 0)
    {
        Buffer->PrivateData = ExAllocatePoolWithTag(NonPagedPool,
                                                    PrivateDataSize,
                                                    TAG_DXGK_SUBMITDMA);
        if (Buffer->PrivateData == NULL)
        {
            ExFreePoolWithTag(Buffer, TAG_DXGK_SUBMITDMA);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Buffer->PrivateData, PrivateDataSize);
        Buffer->PrivateDataSize = PrivateDataSize;
    }
    Status = DxgkVidMmCreateDmaBufferBacking(Adapter, Capacity, SegmentSet, &Allocation);
    if (!NT_SUCCESS(Status))
    {
        if (Buffer->PrivateData != NULL)
            ExFreePoolWithTag(Buffer->PrivateData, TAG_DXGK_SUBMITDMA);
        ExFreePoolWithTag(Buffer, TAG_DXGK_SUBMITDMA);
        return Status;
    }
    Buffer->OwnerAdapter = Adapter;
    Buffer->VirtualAddress = Allocation->SystemMemory;
    Buffer->Capacity = Capacity;
    Buffer->SegmentSet = SegmentSet;
    Buffer->SegmentId = Allocation->SegmentId;
    Buffer->SegmentAddress = Allocation->PhysicalAddress;
    Buffer->BackingKind = DxgkDmaBackingVidMm;
    Buffer->BackingAllocation = Allocation;
    *OutDmaBuffer = Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkAllocateDmaBufferInSegmentSet(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Capacity,
    _In_ ULONG SegmentSet,
    _Out_ PDXGKRNL_DMA_BUFFER *OutDmaBuffer)
{
    return DxgkAllocateDmaBufferInSegmentSetWithPrivateData(Adapter,
                                                            Capacity,
                                                            SegmentSet,
                                                            0,
                                                            OutDmaBuffer);
}

NTSTATUS
DxgkAllocateVirtualDmaBufferWithPrivateData(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ ULONG Capacity,
    _In_ ULONG SegmentSet,
    _In_ ULONG PrivateDataSize,
    _Out_ PDXGKRNL_DMA_BUFFER *OutDmaBuffer)
{
    PDXGKRNL_DMA_BUFFER Buffer;
    NTSTATUS Status;

    if (Device == NULL || Capacity == 0 || OutDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutDmaBuffer = NULL;
    Buffer = DxgkpTakeCachedDmaBuffer(Device->Adapter,
                                      Capacity,
                                      PrivateDataSize,
                                      DxgkDmaBackingVidMm,
                                      SegmentSet,
                                      Device);
    if (Buffer != NULL)
    {
        *OutDmaBuffer = Buffer;
        return STATUS_SUCCESS;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Buffer), TAG_DXGK_SUBMITDMA);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Buffer, sizeof(*Buffer));
    InitializeListHead(&Buffer->CacheListEntry);
    if (PrivateDataSize != 0)
    {
        Buffer->PrivateData = ExAllocatePoolWithTag(NonPagedPool,
                                                    PrivateDataSize,
                                                    TAG_DXGK_SUBMITDMA);
        if (Buffer->PrivateData == NULL)
        {
            ExFreePoolWithTag(Buffer, TAG_DXGK_SUBMITDMA);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Buffer->PrivateData, PrivateDataSize);
        Buffer->PrivateDataSize = PrivateDataSize;
    }
    Status = DxgkVidMmCreateVirtualDmaBufferBacking(Device, Capacity, SegmentSet, &Buffer->BackingAllocation, &Buffer->VirtualBacking, &Buffer->GpuVirtualAddress);
    if (!NT_SUCCESS(Status))
    {
        if (Buffer->PrivateData != NULL)
            ExFreePoolWithTag(Buffer->PrivateData, TAG_DXGK_SUBMITDMA);
        ExFreePoolWithTag(Buffer, TAG_DXGK_SUBMITDMA);
        return Status;
    }
    Buffer->OwnerAdapter = Device->Adapter;
    Buffer->OwnerDevice = Device;
    Buffer->VirtualAddress = Buffer->BackingAllocation->SystemMemory;
    Buffer->Capacity = Capacity;
    Buffer->SegmentSet = SegmentSet;
    Buffer->SegmentId = Buffer->BackingAllocation->SegmentId;
    Buffer->SegmentAddress = Buffer->BackingAllocation->PhysicalAddress;
    Buffer->BackingKind = DxgkDmaBackingVidMm;
    *OutDmaBuffer = Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkAllocateVirtualDmaBuffer(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ ULONG Capacity,
    _In_ ULONG SegmentSet,
    _Out_ PDXGKRNL_DMA_BUFFER *OutDmaBuffer)
{
    return DxgkAllocateVirtualDmaBufferWithPrivateData(Device,
                                                        Capacity,
                                                        SegmentSet,
                                                        0,
                                                        OutDmaBuffer);
}

static VOID
DxgkpDestroyDmaBuffer(
    _In_ PDXGKRNL_DMA_BUFFER DmaBuffer)
{
    if (DmaBuffer->VirtualAddress != NULL &&
        DmaBuffer->BackingKind == DxgkDmaBackingContiguousMemory)
    {
        MmFreeContiguousMemory(DmaBuffer->VirtualAddress);
    }
    if (DmaBuffer->VirtualBacking != NULL)
    {
        DxgkVidMmFreeVirtualDmaBufferBacking(DmaBuffer->VirtualBacking);
        DmaBuffer->VirtualBacking = NULL;
        DmaBuffer->BackingAllocation = NULL;
    }
    else if (DmaBuffer->BackingKind == DxgkDmaBackingVidMm)
    {
        DxgkVidMmReleaseSubmissionResidencyPin(DmaBuffer->BackingAllocation);
        DxgkVidMmDereferenceAllocation(DmaBuffer->BackingAllocation);
        DmaBuffer->BackingAllocation = NULL;
    }
    DmaBuffer->OwnerAdapter = NULL;
    DmaBuffer->OwnerDevice = NULL;
    DmaBuffer->VirtualAddress = NULL;
    if (DmaBuffer->PrivateData != NULL)
    {
        ExFreePoolWithTag(DmaBuffer->PrivateData, TAG_DXGK_SUBMITDMA);
        DmaBuffer->PrivateData = NULL;
    }
    DmaBuffer->BackingKind = DxgkDmaBackingInvalid;
    ExFreePoolWithTag(DmaBuffer, TAG_DXGK_SUBMITDMA);
}

VOID
NTAPI
DxgkFreeDmaBuffer(
    _In_opt_ PDXGKRNL_DMA_BUFFER DmaBuffer)
{
    PDXGKRNL_ADAPTER Adapter;
    LIST_ENTRY FreeList;
    BOOLEAN VirtualMappingsReusable;
    BOOLEAN Cached = FALSE;
    ULONG Charge;
    KIRQL OldIrql;

    if (DmaBuffer == NULL)
        return;

    Adapter = DmaBuffer->OwnerAdapter;
    DmaBuffer->SubmissionStartOffset = 0;
    DmaBuffer->SubmissionEndOffset = 0;
    VirtualMappingsReusable = DmaBuffer->VirtualBacking == NULL;
    if (DmaBuffer->VirtualBacking != NULL &&
        DmaBuffer->OwnerDevice != NULL &&
        InterlockedCompareExchange(&DmaBuffer->OwnerDevice->Destroying,
                                   0,
                                   0) == 0)
    {
        /* The submission's pins on the source/destination mappings end
         * here; the mappings stay on the pooled backing for the next
         * Present through the same bindings. */
        DxgkVidMmUnpinVirtualDmaBufferMappings(DmaBuffer->VirtualBacking);
        VirtualMappingsReusable = TRUE;
    }
    if (Adapter != NULL &&
        DmaBuffer->VirtualAddress != NULL &&
        VirtualMappingsReusable &&
        (DmaBuffer->BackingKind == DxgkDmaBackingContiguousMemory ||
         (DmaBuffer->BackingKind == DxgkDmaBackingVidMm &&
          DmaBuffer->BackingAllocation != NULL)) &&
        DmaBuffer->Capacity != 0 &&
        DmaBuffer->Capacity <= DXGKP_DMA_BUFFER_CACHE_MAX_CAPACITY)
    {
        InitializeListHead(&FreeList);
        Charge = DxgkpDmaBufferCacheCharge(DmaBuffer);
        KeAcquireSpinLock(&Adapter->DmaBufferCacheLock, &OldIrql);
        if (InterlockedCompareExchange(&Adapter->DmaBufferCacheStopping, 0, 0) == 0)
        {
            while (Adapter->DmaBufferCacheCount >= DXGKP_DMA_BUFFER_CACHE_LIMIT ||
                   Adapter->DmaBufferCacheBytes > DXGKP_DMA_BUFFER_CACHE_MAX_BYTES - Charge)
            {
                PLIST_ENTRY Link;
                PDXGKRNL_DMA_BUFFER EvictedBuffer;

                /* Entries are exact-keyed by size, private-data size,
                 * backing kind, and segment set. A larger cached buffer
                 * cannot satisfy a smaller request, so retaining it by size
                 * can permanently starve a new key. Free inserts at the tail
                 * and reuse cycles an entry through the tail; evict the head
                 * as the least-recently-used entry. */
                Link = RemoveHeadList(&Adapter->DmaBufferCacheListHead);
                EvictedBuffer = CONTAINING_RECORD(Link,
                                                   DXGKRNL_DMA_BUFFER,
                                                   CacheListEntry);
                InsertTailList(&FreeList, Link);
                Adapter->DmaBufferCacheCount--;
                ASSERT(Adapter->DmaBufferCacheBytes >= DxgkpDmaBufferCacheCharge(EvictedBuffer));
                Adapter->DmaBufferCacheBytes -= DxgkpDmaBufferCacheCharge(EvictedBuffer);
            }

            InsertTailList(&Adapter->DmaBufferCacheListHead, &DmaBuffer->CacheListEntry);
            Adapter->DmaBufferCacheCount++;
            Adapter->DmaBufferCacheBytes += Charge;
            Cached = TRUE;
        }
        KeReleaseSpinLock(&Adapter->DmaBufferCacheLock, OldIrql);

        while (!IsListEmpty(&FreeList))
        {
            PDXGKRNL_DMA_BUFFER EvictedBuffer;

            EvictedBuffer = CONTAINING_RECORD(RemoveHeadList(&FreeList), DXGKRNL_DMA_BUFFER, CacheListEntry);
            InitializeListHead(&EvictedBuffer->CacheListEntry);
            DxgkpDestroyDmaBuffer(EvictedBuffer);
        }
        if (Cached)
            return;
    }

    DxgkpDestroyDmaBuffer(DmaBuffer);
}

VOID
DxgkPurgeDmaBufferCacheForDevice(
    _In_ PDXGKRNL_DEVICE Device)
{
    PDXGKRNL_ADAPTER Adapter;
    LIST_ENTRY FreeList;
    PLIST_ENTRY Link;
    PLIST_ENTRY Next;
    KIRQL OldIrql;

    if (Device == NULL || Device->Adapter == NULL)
        return;
    Adapter = Device->Adapter;
    InitializeListHead(&FreeList);

    KeAcquireSpinLock(&Adapter->DmaBufferCacheLock, &OldIrql);
    for (Link = Adapter->DmaBufferCacheListHead.Flink;
         Link != &Adapter->DmaBufferCacheListHead;
         Link = Next)
    {
        PDXGKRNL_DMA_BUFFER DmaBuffer;

        Next = Link->Flink;
        DmaBuffer = CONTAINING_RECORD(Link,
                                      DXGKRNL_DMA_BUFFER,
                                      CacheListEntry);
        if (DmaBuffer->OwnerDevice != Device)
            continue;
        RemoveEntryList(Link);
        InsertTailList(&FreeList, Link);
        ASSERT(Adapter->DmaBufferCacheCount != 0);
        Adapter->DmaBufferCacheCount--;
        ASSERT(Adapter->DmaBufferCacheBytes >= DxgkpDmaBufferCacheCharge(DmaBuffer));
        Adapter->DmaBufferCacheBytes -= DxgkpDmaBufferCacheCharge(DmaBuffer);
    }
    KeReleaseSpinLock(&Adapter->DmaBufferCacheLock, OldIrql);

    while (!IsListEmpty(&FreeList))
    {
        PDXGKRNL_DMA_BUFFER DmaBuffer;

        DmaBuffer = CONTAINING_RECORD(RemoveHeadList(&FreeList),
                                      DXGKRNL_DMA_BUFFER,
                                      CacheListEntry);
        InitializeListHead(&DmaBuffer->CacheListEntry);
        DxgkpDestroyDmaBuffer(DmaBuffer);
    }
}

static VOID
DxgkpDrainDmaBufferCache(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY FreeList;
    KIRQL OldIrql;

    InitializeListHead(&FreeList);
    InterlockedExchange(&Adapter->DmaBufferCacheStopping, 1);
    KeAcquireSpinLock(&Adapter->DmaBufferCacheLock, &OldIrql);
    while (!IsListEmpty(&Adapter->DmaBufferCacheListHead))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->DmaBufferCacheListHead);
        PDXGKRNL_DMA_BUFFER DmaBuffer = CONTAINING_RECORD(Link, DXGKRNL_DMA_BUFFER, CacheListEntry);

        InsertTailList(&FreeList, Link);
        ASSERT(Adapter->DmaBufferCacheCount != 0);
        Adapter->DmaBufferCacheCount--;
        ASSERT(Adapter->DmaBufferCacheBytes >= DxgkpDmaBufferCacheCharge(DmaBuffer));
        Adapter->DmaBufferCacheBytes -= DxgkpDmaBufferCacheCharge(DmaBuffer);
    }
    KeReleaseSpinLock(&Adapter->DmaBufferCacheLock, OldIrql);

    while (!IsListEmpty(&FreeList))
    {
        PDXGKRNL_DMA_BUFFER DmaBuffer;

        DmaBuffer = CONTAINING_RECORD(RemoveHeadList(&FreeList), DXGKRNL_DMA_BUFFER, CacheListEntry);
        InitializeListHead(&DmaBuffer->CacheListEntry);
        DxgkpDestroyDmaBuffer(DmaBuffer);
    }
}

NTSTATUS
NTAPI
DxgkFlushDmaBufferForSubmission(
    _In_ PDXGKRNL_DMA_BUFFER DmaBuffer)
{
    PVOID StartAddress;
    ULONG Length;
    PMDL Mdl;

    if (DmaBuffer == NULL || DmaBuffer->VirtualAddress == NULL ||
        (DmaBuffer->BackingKind != DxgkDmaBackingContiguousMemory &&
         DmaBuffer->BackingKind != DxgkDmaBackingVidMm) ||
        DmaBuffer->SubmissionStartOffset > DmaBuffer->SubmissionEndOffset ||
        DmaBuffer->SubmissionEndOffset > DmaBuffer->Capacity)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Length = DmaBuffer->SubmissionEndOffset -
             DmaBuffer->SubmissionStartOffset;
    if (Length == 0)
        return STATUS_SUCCESS;
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    StartAddress = (PUCHAR)DmaBuffer->VirtualAddress +
                   DmaBuffer->SubmissionStartOffset;
    Mdl = IoAllocateMdl(StartAddress, Length, FALSE, FALSE, NULL);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    MmBuildMdlForNonPagedPool(Mdl);
    KeFlushIoBuffers(Mdl, FALSE, TRUE);
    IoFreeMdl(Mdl);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

static VOID DxgkpAssertSubmitDmaReservationInvariantLocked(_In_ PDXGKRNL_ADAPTER Adapter)
{
    ASSERT(Adapter->SubmitDmaActiveReservations >= 0);
    ASSERT((KeReadStateEvent(&Adapter->SubmitDmaReservationsDrainedEvent) != 0) == DxgkSubmitReservationCoreIsDrainedLocked(&Adapter->SubmitDmaActiveReservations));
}

static BOOLEAN DxgkpAcquireSubmitDmaReservation(_In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN Acquired;
    BOOLEAN ClearDrainedEvent;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    Acquired = DxgkSubmitReservationCoreTryAcquireLocked(&Adapter->SubmitDmaActiveReservations, InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0, &ClearDrainedEvent);
    if (Acquired && ClearDrainedEvent)
        KeClearEvent(&Adapter->SubmitDmaReservationsDrainedEvent);
    DxgkpAssertSubmitDmaReservationInvariantLocked(Adapter);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    return Acquired;
}

static VOID DxgkpReleaseSubmitDmaReservationLocked(_In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN Released;
    BOOLEAN SetDrainedEvent;

    Released = DxgkSubmitReservationCoreReleaseLocked(&Adapter->SubmitDmaActiveReservations, &SetDrainedEvent);
    ASSERT(Released);
    if (SetDrainedEvent)
        KeSetEvent(&Adapter->SubmitDmaReservationsDrainedEvent, IO_NO_INCREMENT, FALSE);
    DxgkpAssertSubmitDmaReservationInvariantLocked(Adapter);
}

static VOID DxgkpReleaseSubmitDmaReservation(_In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    DxgkpReleaseSubmitDmaReservationLocked(Adapter);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
}

VOID NTAPI DxgkWaitForSubmitDmaReservations(_In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN Drained;
    KIRQL OldIrql;

    PAGED_CODE();
    if (Adapter == NULL)
        return;
    for (;;)
    {
        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        ASSERT(InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0);
        Drained = DxgkSubmitReservationCoreIsDrainedLocked(&Adapter->SubmitDmaActiveReservations);
        DxgkpAssertSubmitDmaReservationInvariantLocked(Adapter);
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        if (Drained)
            return;
        KeWaitForSingleObject(&Adapter->SubmitDmaReservationsDrainedEvent, Executive, KernelMode, FALSE, NULL);
    }
}

static VOID NTAPI DxgkpTrackedWorkAdjustInFlight(_In_opt_ PVOID Context, _In_ LONG Delta)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry = Context;
    BOOLEAN Changed;

    if (Entry == NULL || Entry->Device == NULL || Entry->Device->ProcessRecord == NULL)
        return;
    if (Delta > 0)
    {
        Changed = DxgkSubmissionAccountingCommitLocked(&Entry->SubmissionAccounting, &Entry->Device->InFlightSubmissions, &Entry->Device->ProcessRecord->InFlightSubmissions);
        ASSERT(Changed);
    }
    else if (Delta < 0)
    {
        Changed = DxgkSubmissionAccountingRelease(&Entry->SubmissionAccounting, &Entry->Device->InFlightSubmissions, &Entry->Device->ProcessRecord->InFlightSubmissions);
        ASSERT(Changed);
    }
}

static NTSTATUS DxgkpPrechargeTrackedSubmission(_In_ PDXGKRNL_ADAPTER Adapter, _Inout_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    BOOLEAN Charged;
    KIRQL OldIrql;

    if (Adapter == NULL || Entry == NULL || Entry->Device == NULL || Entry->Device->ProcessRecord == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    Charged = DxgkSubmissionAccountingTryPrechargeLocked(&Entry->SubmissionAccounting, &Entry->Device->InFlightSubmissions, &Entry->Device->ProcessRecord->InFlightSubmissions, DXGK_DEVICE_MAX_INFLIGHT, DXGK_PROCESS_MAX_INFLIGHT);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    return Charged ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
}

static VOID NTAPI DxgkpTrackedWorkPublishSignal(_In_opt_ PVOID Context)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry = Context;

    if (Entry != NULL &&
        Entry->SignalSyncObjectReference != NULL
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
        && !Entry->SignalWrittenByGpu
#endif
        )
    {
        DxgkSyncObjectPublishTrackedSignal(Entry->SignalSyncObjectReference, Entry->SignalFenceValue);
    }
}

static VOID NTAPI DxgkpTrackedWorkCompleteDeviceWork(_In_opt_ PVOID Context)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry = Context;

    /* A present is not complete until its passive scanout step has run.
     * GPU fences still retire here; only the device-work acknowledgement is
     * deferred to DxgkpFreeTrackedDmaBufferEntry. */
    if (Entry != NULL && !Entry->RefreshSharedPrimaryOnRetire &&
        !Entry->ProgramSourceScanoutOnRetire)
        DxgkDeviceWorkComplete(Entry->DeviceWork);
}

static const DXGK_TRACKED_WORK_CALLBACKS DxgkpTrackedWorkCallbacks = { DxgkpTrackedWorkAdjustInFlight, DxgkpTrackedWorkPublishSignal, DxgkpTrackedWorkCompleteDeviceWork };

static VOID DxgkpFreeTrackedDmaBufferEntry(_In_opt_ PDXGKRNL_ADAPTER Adapter, _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry, _In_ BOOLEAN Completed, _In_ BOOLEAN FreeDmaBuffer, _In_ BOOLEAN MiniportCallbacksValid);

static NTSTATUS
DxgkpReferenceTrackedAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation)
{
    if (Adapter == NULL || Allocation == NULL || OutAllocation == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutAllocation = NULL;
    if (Allocation->Adapter != Adapter || !DxgkVidMmDuplicateAllocationReference(Allocation))
        return STATUS_INVALID_HANDLE;
    *OutAllocation = Allocation;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpReferenceTrackedLogicalAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation)
{
    if (Adapter == NULL || Allocation == NULL || OutAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocation = NULL;
    if (Allocation->Adapter != Adapter || !DxgkVidMmDuplicateLogicalReference(Allocation))
        return STATUS_INVALID_HANDLE;
    *OutAllocation = Allocation;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkPrepareTrackedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const DXGKRNL_TRACK_DMA_ARGS *Args,
    _Out_ PDXGKRNL_SUBMIT_DMA_BUFFER *OutEntry)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Entry;
    NTSTATUS Status;
    UINT Index;

    if (OutEntry == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutEntry = NULL;
    if (Adapter == NULL || Args == NULL || Args->DmaBuffer == NULL || Args->SubmissionFenceId == 0 || Args->NodeOrdinal >= Adapter->NodeCount || Args->NodeOrdinal >= DXGK_MAX_TRACKED_NODES || Args->DmaBuffer->VirtualAddress == NULL || Args->DmaBuffer->SubmissionStartOffset > Args->DmaBuffer->SubmissionEndOffset || Args->DmaBuffer->SubmissionEndOffset > Args->DmaBuffer->Capacity || (Args->PresentBindingReferenceCount != 0 && (Args->PresentBindingReferences == NULL || Args->Device == NULL)) || (Args->OpenBindingReferenceCount != 0 && Args->OpenBindingReferences == NULL) || (Args->AllocationReferenceCount != 0 && Args->AllocationReferences == NULL) || (Args->LifetimeAllocationReferenceCount != 0 && Args->LifetimeAllocationReferences == NULL) || (SIZE_T)Args->PresentBindingReferenceCount > MAXULONG_PTR / sizeof(PDXGKVMM_ALLOCATION) || (SIZE_T)Args->OpenBindingReferenceCount > MAXULONG_PTR / sizeof(PDXGKVMM_ALLOCATION) || (SIZE_T)Args->AllocationReferenceCount > MAXULONG_PTR / sizeof(PDXGKVMM_ALLOCATION) || (SIZE_T)Args->LifetimeAllocationReferenceCount > MAXULONG_PTR / sizeof(PDXGKVMM_ALLOCATION))
        return STATUS_INVALID_PARAMETER;
    if (!DxgkpAcquireSubmitDmaReservation(Adapter))
        return STATUS_DELETE_PENDING;

    Entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Entry), TAG_DXGK_SUBMITDMA);
    if (Entry == NULL)
    {
        DxgkpReleaseSubmitDmaReservation(Adapter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Entry, sizeof(*Entry));
    InitializeListHead(&Entry->ListEntry);
    DxgkTrackedWorkCoreInitialize(&Entry->TrackedWork, &DxgkpTrackedWorkCallbacks, Entry, FALSE);
    DxgkSubmissionAccountingInitialize(&Entry->SubmissionAccounting);

    Entry->Adapter = Adapter;
    Entry->ReservationActive = 1;
    if (Args->HoldSharedSurfaceRundown)
    {
        if (!ExAcquireRundownProtection(&Adapter->SharedSurfaceRundown))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_DELETE_PENDING;
        }
        Entry->SharedSurfaceRundownHeld = TRUE;
    }
    Entry->SubmissionFenceId = Args->SubmissionFenceId;
    Entry->NodeOrdinal = Args->NodeOrdinal;
    Entry->EngineOrdinal = Args->EngineOrdinal;
    Entry->SignalFenceValue = Args->SignalFenceValue;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    Entry->SignalWrittenByGpu = Args->SignalWrittenByGpu;
    if (Entry->SignalWrittenByGpu &&
        Args->SignalSyncObjectReference == NULL)
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_INVALID_PARAMETER;
    }
#endif
    Entry->RefreshPresentId = Args->PresentId;
    Entry->DmaBuffer = Args->DmaBuffer;
    if (Args->Device != NULL && !DxgkReferenceDevice(Args->Device))
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_DELETE_PENDING;
    }
    Entry->Device = Args->Device;
    if (Entry->Device != NULL && Entry->Device->ProcessRecord == NULL)
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Args->EnforceSubmissionQuota)
    {
        Status = DxgkpPrechargeTrackedSubmission(Adapter, Entry);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->DeviceWork != NULL)
    {
        if (Entry->Device == NULL || Args->DeviceWork->Device != Entry->Device)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INVALID_PARAMETER;
        }
        Entry->DeviceWork = Args->DeviceWork;
    }
    else if (Entry->Device != NULL)
    {
        Status = DxgkDeviceWorkCreate(Entry->Device, &Entry->DeviceWork);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
        Status = DxgkTrackedWorkCoreClaimDeviceWork(&Entry->TrackedWork) ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    if (Args->SignalSyncObjectReference != NULL)
    {
        Status =
            DxgkSyncObjectDuplicateTrackedSignal(
                Args->SignalSyncObjectReference,
                Entry->Device,
                &Entry->SignalSyncObjectReference);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    else
#endif
    if (Args->hSignalSyncObject != 0)
    {
        Status = DxgkSyncObjectReferenceTrackedSignal(Args->hSignalSyncObject, Entry->Device, &Entry->SignalSyncObjectReference);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->Context != NULL && !DxgkReferenceContext(Args->Context))
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_DELETE_PENDING;
    }
    Entry->Context = Args->Context;
    if (Entry->Context != NULL && (Entry->Context->Device == NULL || Entry->Context->Device->Adapter != Adapter || (Entry->Device != NULL && Entry->Context->Device != Entry->Device)))
    {
        DxgkCancelTrackedDmaBuffer(Entry);
        return STATUS_INVALID_PARAMETER;
    }
    Entry->SourceAllocationHandle = Args->SourceAllocationHandle;
    Entry->RefreshAllocationHandle = Args->RefreshAllocationHandle;
    if (Args->SourceAllocation != NULL)
    {
        Status = DxgkpReferenceTrackedAllocation(Adapter, Args->SourceAllocation, &Entry->SourceAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    else if (Args->SourceAllocationHandle != NULL)
    {
        Status = DxgkVidMmReferenceAllocation(Args->SourceAllocationHandle, Adapter, NULL, &Entry->SourceAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->RefreshAllocation != NULL)
    {
        Status = DxgkpReferenceTrackedAllocation(Adapter, Args->RefreshAllocation, &Entry->RefreshAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    else if (Args->RefreshAllocationHandle != NULL)
    {
        Status = DxgkVidMmReferenceAllocation(Args->RefreshAllocationHandle, Adapter, NULL, &Entry->RefreshAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->SourceOpenBindingReference != NULL)
    {
        Status = DxgkpReferenceTrackedLogicalAllocation(Adapter, Args->SourceOpenBindingReference, &Entry->SourceOpenBindingReference);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    if (Args->DestinationOpenBindingReference != NULL)
    {
        Status = DxgkpReferenceTrackedLogicalAllocation(Adapter, Args->DestinationOpenBindingReference, &Entry->DestinationOpenBindingReference);
        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return Status;
        }
    }
    Entry->RefreshVidPnSourceId = Args->RefreshVidPnSourceId;
    if (Args->RefreshDstRect != NULL)
        Entry->RefreshDstRect = *Args->RefreshDstRect;
    else
        RtlZeroMemory(&Entry->RefreshDstRect, sizeof(Entry->RefreshDstRect));
    Entry->RefreshSharedPrimaryOnRetire = Args->RefreshIsSharedPrimary && Args->RefreshAllocationHandle != NULL;
    Entry->ProgramSourceScanoutOnRetire =
        Args->ProgramSourceScanoutOnRetire &&
        Args->SourceAllocationHandle != NULL;
    Entry->SharedSurfaceGeneration = Args->SharedSurfaceGeneration;
    Entry->SourceIsSharedPrimary = Args->SourceIsSharedPrimary;
    Entry->SourceIsSharedShadow = Args->SourceIsSharedShadow;
    Entry->SourceWidth = Args->SourceWidth;
    Entry->SourceHeight = Args->SourceHeight;
    Entry->SourcePitch = Args->SourcePitch;
    Entry->RefreshWidth = Args->RefreshWidth;
    Entry->RefreshHeight = Args->RefreshHeight;
    Entry->PresentBindingReferenceCount = 0;
    Entry->PresentBindingReferenceList = NULL;
    Entry->OpenBindingReferenceCount = 0;
    Entry->OpenBindingReferenceList = NULL;
    Entry->AllocationReferenceCount = 0;
    Entry->AllocationReferenceList = NULL;
    Entry->LifetimeAllocationReferenceCount = 0;
    Entry->LifetimeAllocationReferenceList = NULL;

    if (Args->PresentBindingReferenceCount != 0)
    {
        Entry->PresentBindingReferenceList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Args->PresentBindingReferenceCount * sizeof(*Entry->PresentBindingReferenceList), TAG_DXGK_SUBMITDMA);
        if (Entry->PresentBindingReferenceList == NULL)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Entry->PresentBindingReferenceList, (SIZE_T)Args->PresentBindingReferenceCount * sizeof(*Entry->PresentBindingReferenceList));
        for (Index = 0; Index < Args->PresentBindingReferenceCount; ++Index)
        {
            Status = DxgkpReferenceTrackedLogicalAllocation(Adapter, Args->PresentBindingReferences[Index], &Entry->PresentBindingReferenceList[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Entry->PresentBindingReferenceCount++;
        }
    }

    if (Args->OpenBindingReferenceCount != 0)
    {
        Entry->OpenBindingReferenceList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Args->OpenBindingReferenceCount * sizeof(*Entry->OpenBindingReferenceList), TAG_DXGK_SUBMITDMA);
        if (Entry->OpenBindingReferenceList == NULL)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Entry->OpenBindingReferenceList, (SIZE_T)Args->OpenBindingReferenceCount * sizeof(*Entry->OpenBindingReferenceList));
        for (Index = 0; Index < Args->OpenBindingReferenceCount; ++Index)
        {
            Status = DxgkpReferenceTrackedLogicalAllocation(Adapter, Args->OpenBindingReferences[Index], &Entry->OpenBindingReferenceList[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Entry->OpenBindingReferenceCount++;
        }
    }

    if (Args->AllocationReferenceCount != 0)
    {
        Entry->AllocationReferenceList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Args->AllocationReferenceCount * sizeof(*Entry->AllocationReferenceList), TAG_DXGK_SUBMITDMA);
        if (Entry->AllocationReferenceList == NULL)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Entry->AllocationReferenceList, (SIZE_T)Args->AllocationReferenceCount * sizeof(*Entry->AllocationReferenceList));
        for (Index = 0; Index < Args->AllocationReferenceCount; ++Index)
        {
            Status = DxgkpReferenceTrackedAllocation(Adapter, Args->AllocationReferences[Index], &Entry->AllocationReferenceList[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            /* Render may update allocation-backed command data.  Clean once,
             * after Render and immediately before the tracker owns the final
             * residency pin.  A supplied dirty vector lets V2 escapes avoid
             * walking read-only resources. */
            Status = DxgkVidMmAcquireSubmissionResidencyPinEx(
                         Entry->AllocationReferenceList[Index],
                         Adapter,
                         NULL,
                         Args->AllocationCpuDirty == NULL ||
                             Args->AllocationCpuDirty[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkVidMmDereferenceAllocation(Entry->AllocationReferenceList[Index]);
                Entry->AllocationReferenceList[Index] = NULL;
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Entry->AllocationReferenceCount++;
        }
    }

    if (Args->LifetimeAllocationReferenceCount != 0)
    {
        Entry->LifetimeAllocationReferenceList =
            ExAllocatePoolWithTag(
                NonPagedPool,
                (SIZE_T)Args->LifetimeAllocationReferenceCount *
                    sizeof(*Entry->LifetimeAllocationReferenceList),
                TAG_DXGK_SUBMITDMA);
        if (Entry->LifetimeAllocationReferenceList == NULL)
        {
            DxgkCancelTrackedDmaBuffer(Entry);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(
            Entry->LifetimeAllocationReferenceList,
            (SIZE_T)Args->LifetimeAllocationReferenceCount *
                sizeof(*Entry->LifetimeAllocationReferenceList));
        for (Index = 0;
             Index < Args->LifetimeAllocationReferenceCount;
             ++Index)
        {
            Status = DxgkpReferenceTrackedAllocation(
                         Adapter,
                         Args->LifetimeAllocationReferences[Index],
                         &Entry->LifetimeAllocationReferenceList[Index]);
            if (!NT_SUCCESS(Status))
            {
                DxgkCancelTrackedDmaBuffer(Entry);
                return Status;
            }
            Entry->LifetimeAllocationReferenceCount++;
        }
    }

    *OutEntry = Entry;
    return STATUS_SUCCESS;
}

VOID
NTAPI
DxgkCommitTrackedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    BOOLEAN CompletionAlreadyReached;
    BOOLEAN RetiredNow;
    KIRQL OldIrql;
    ULONG NodeFence;

    if (Adapter == NULL || Entry == NULL || Entry->Adapter != Adapter)
        return;
    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    /*
     * Stamp every allocation this command references with the submission
     * fence, so destruction can wait for the work that actually touches the
     * allocation rather than for the destroying device's queue alone.
     */
    if (Entry->NodeOrdinal < DXGK_MAX_TRACKED_NODES &&
        Entry->AllocationReferenceList != NULL)
    {
        UINT RefIndex;

        for (RefIndex = 0; RefIndex < Entry->AllocationReferenceCount; ++RefIndex)
        {
            PDXGKVMM_ALLOCATION Referenced =
                Entry->AllocationReferenceList[RefIndex];

            if (Referenced == NULL)
                continue;
            LONG Epoch =
                InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityEpoch, 0, 0);

            if (Referenced->LastRefEpoch != Epoch)
            {
                /* Older stamps name fence ids from a reset timeline. */
                RtlZeroMemory((PVOID)Referenced->LastRefFenceId,
                              sizeof(Referenced->LastRefFenceId));
                Referenced->LastRefEpoch = Epoch;
            }
            if (Referenced->LastRefFenceId[Entry->NodeOrdinal] == 0 ||
                !DxgkpFenceIdReached(Referenced->LastRefFenceId[Entry->NodeOrdinal],
                                     Entry->SubmissionFenceId))
            {
                Referenced->LastRefFenceId[Entry->NodeOrdinal] =
                    Entry->SubmissionFenceId;
            }
        }
    }
    NodeFence = Adapter->NodeLastCompletedFenceId[Entry->NodeOrdinal];
    CompletionAlreadyReached = NodeFence != 0 && DxgkpFenceIdReached(NodeFence, Entry->SubmissionFenceId);
    if (!DxgkTrackedWorkCoreCommit(&Entry->TrackedWork, CompletionAlreadyReached, &RetiredNow))
    {
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        return;
    }
    if (RetiredNow)
    {
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        Entry->CleanupAsCompleted = TRUE;
#endif
        InsertTailList(&Adapter->SubmitDmaRetireListHead, &Entry->ListEntry);
        KeClearEvent(&Adapter->SubmitDmaRetireDrainedEvent);
    }
    else
    {
        InsertTailList(&Adapter->SubmitDmaListHead, &Entry->ListEntry);
    }
    if (InterlockedExchange(&Entry->ReservationActive, 0) != 0)
        DxgkpReleaseSubmitDmaReservationLocked(Adapter);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    DxgkRetireCompletedDmaBuffers(Adapter);
}

VOID
NTAPI
DxgkAdoptTrackedDmaBuffer(
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    if (Entry != NULL)
        (VOID)DxgkTrackedWorkCoreClaimExternalCleanup(&Entry->TrackedWork);
}

NTSTATUS
NTAPI
DxgkActivateTrackedDmaBuffer(
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    NTSTATUS Status;

    if (Entry == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Entry->DeviceWork == NULL)
        return STATUS_SUCCESS;
    Status = DxgkDeviceWorkActivate(Entry->DeviceWork);
    if (NT_SUCCESS(Status) && !DxgkTrackedWorkCoreClaimDeviceWork(&Entry->TrackedWork))
        return STATUS_INVALID_DEVICE_STATE;
    return Status;
}

VOID
NTAPI
DxgkCancelTrackedDmaBuffer(
    _In_opt_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    PDXGKRNL_ADAPTER Adapter;
    BOOLEAN Cancelled;
    BOOLEAN FreeDmaBuffer;
    DXGK_TRACKED_WORK_STATE PreviousState;
    KIRQL OldIrql;

    if (Entry == NULL)
        return;

    Adapter = Entry->Adapter;
    if (Adapter != NULL)
    {
        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        PreviousState = DxgkTrackedWorkCoreGetState(&Entry->TrackedWork);
        Cancelled = DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
        if (Cancelled && PreviousState == DxgkTrackedWorkCommitted)
        {
            RemoveEntryList(&Entry->ListEntry);
            InitializeListHead(&Entry->ListEntry);
        }
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
    }
    else
    {
        PreviousState = DxgkTrackedWorkCoreGetState(&Entry->TrackedWork);
        Cancelled = DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
    }
    if (!Cancelled)
        return;
    FreeDmaBuffer = PreviousState == DxgkTrackedWorkCommitted;
    if (Adapter != NULL && InterlockedExchange(&Entry->ReservationActive, 0) != 0)
        DxgkpReleaseSubmitDmaReservation(Adapter);
    DxgkpFreeTrackedDmaBufferEntry(Adapter, Entry, FALSE, FreeDmaBuffer, TRUE);
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
/*
 * Terminalize one miniport-accepted DMA buffer without pretending that its
 * fence completed.  The tracked-work cancellation is DPC-safe and owns the
 * exactly-once in-flight/device-work transition.  Allocation references,
 * DMA storage, and fence identity are left for the existing PASSIVE worker.
 */
BOOLEAN
NTAPI
DxgkFailTrackedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    DXGK_TRACKED_WORK_STATE PreviousState;
    BOOLEAN Cancelled;
    BOOLEAN QueueWorker = FALSE;
    LONG ActiveWorkers;
    KIRQL OldIrql;

    if (Adapter == NULL || Entry == NULL || Entry->Adapter != Adapter)
        return FALSE;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    PreviousState = DxgkTrackedWorkCoreGetState(&Entry->TrackedWork);
    Cancelled = DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
    if (!Cancelled)
    {
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        return FALSE;
    }

    if (PreviousState == DxgkTrackedWorkCommitted)
    {
        RemoveEntryList(&Entry->ListEntry);
        InitializeListHead(&Entry->ListEntry);
    }
    ASSERT(PreviousState == DxgkTrackedWorkPrepared ||
           PreviousState == DxgkTrackedWorkCommitted);
    ASSERT(DxgkTrackedWorkCoreOwnsExternalCleanup(&Entry->TrackedWork));

    Entry->CleanupAsCompleted = FALSE;
    InsertTailList(&Adapter->SubmitDmaRetireListHead, &Entry->ListEntry);
    if (InterlockedExchange(&Entry->ReservationActive, 0) != 0)
        DxgkpReleaseSubmitDmaReservationLocked(Adapter);
    KeClearEvent(&Adapter->SubmitDmaRetireDrainedEvent);
    if (InterlockedCompareExchange(
            &Adapter->SubmitDmaRetireWorkQueued, 1, 0) == 0)
    {
        ActiveWorkers =
            InterlockedIncrement(&Adapter->SubmitDmaRetireActiveWorkers);
        ASSERT(ActiveWorkers == 1);
        QueueWorker = TRUE;
    }
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    if (QueueWorker)
        ExQueueWorkItem(
            &Adapter->SubmitDmaRetireWorkItem,
            DelayedWorkQueue);
    return TRUE;
}
#endif

static ULONG
DxgkpTrackedSamplePitch(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG DefaultPitch)
{
    SIZE_T CandidatePitch;

    if (DefaultPitch != 0)
        return DefaultPitch;

    if (Allocation != NULL &&
        Height != 0 &&
        Allocation->Size >= (SIZE_T)Width * sizeof(ULONG) &&
        (Allocation->Size % Height) == 0)
    {
        CandidatePitch = Allocation->Size / Height;
        if (CandidatePitch >= (SIZE_T)Width * sizeof(ULONG) &&
            CandidatePitch <= MAXULONG)
        {
            return (ULONG)CandidatePitch;
        }
    }

    return Width * sizeof(ULONG);
}

static VOID
DxgkpTraceTrackedSurfaceSample(
    _In_z_ PCSTR SurfaceTag,
    _In_ ULONG64 PresentId,
    _In_ ULONG FenceId,
    _In_ const RECT *Rect,
    _In_reads_bytes_(PitchBytes * Height) const VOID *Base,
    _In_ ULONG PitchBytes,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    const ULONG *Pixels = (const ULONG *)Base;
    ULONG PitchPixels;
    RECT SampleRect;
    LONG xs[5];
    LONG ys[5];
    ULONG Samples[5];
    ULONG NonZeroCount = 0;
    UINT i;

    if (Rect == NULL ||
        Base == NULL ||
        PitchBytes < sizeof(ULONG) ||
        Width == 0 ||
        Height == 0)
    {
        return;
    }

    SampleRect = *Rect;
    if (SampleRect.left < 0)
        SampleRect.left = 0;
    if (SampleRect.top < 0)
        SampleRect.top = 0;
    if (SampleRect.right > (LONG)Width)
        SampleRect.right = (LONG)Width;
    if (SampleRect.bottom > (LONG)Height)
        SampleRect.bottom = (LONG)Height;
    if (SampleRect.left >= SampleRect.right ||
        SampleRect.top >= SampleRect.bottom)
    {
        return;
    }

    PitchPixels = PitchBytes / sizeof(ULONG);
    xs[0] = SampleRect.left;
    ys[0] = SampleRect.top;
    xs[1] = SampleRect.right - 1;
    ys[1] = SampleRect.top;
    xs[2] = SampleRect.left + ((SampleRect.right - SampleRect.left) / 2);
    ys[2] = SampleRect.top + ((SampleRect.bottom - SampleRect.top) / 2);
    xs[3] = SampleRect.left;
    ys[3] = SampleRect.bottom - 1;
    xs[4] = SampleRect.right - 1;
    ys[4] = SampleRect.bottom - 1;

    for (i = 0; i < RTL_NUMBER_OF(Samples); ++i)
    {
        Samples[i] = Pixels[(ys[i] * PitchPixels) + xs[i]];
        if (Samples[i] != 0)
            ++NonZeroCount;
    }

    DXGKRNL_TRACE("DxgkpTrackedSample[%s]: PresentId=%llu fence=%u "
                  "nz=%lu pitch=%lu rect=(%ld,%ld)-(%ld,%ld) "
                  "tl=%08lx tr=%08lx c=%08lx bl=%08lx br=%08lx\n",
                  SurfaceTag,
                  PresentId,
                  FenceId,
                  NonZeroCount,
                  PitchBytes,
                  SampleRect.left,
                  SampleRect.top,
                  SampleRect.right,
                  SampleRect.bottom,
                  (unsigned long)Samples[0],
                  (unsigned long)Samples[1],
                  (unsigned long)Samples[2],
                  (unsigned long)Samples[3],
                  (unsigned long)Samples[4]);
}

static VOID
DxgkpTraceTrackedRefreshSamples(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry,
    _In_ PDXGKVMM_ALLOCATION DestinationAllocation)
{
    PDXGKVMM_ALLOCATION SourceAllocation;
    PVOID SourceVa = NULL;
    PVOID DestinationVa = NULL;
    ULONG SourcePitch = 0;
    ULONG DestinationPitch = 0;
    ULONG Width;
    ULONG Height;

    if (Adapter == NULL || Entry == NULL || DestinationAllocation == NULL)
        return;

    if (InterlockedIncrement(&DxgkpTrackedSampleTraceCount) > 64)
        return;

    Width = Entry->RefreshWidth;
    Height = Entry->RefreshHeight;
    if (Width == 0 || Height == 0 || Width > (MAXULONG / sizeof(ULONG)))
        return;

    DestinationPitch = DxgkpTrackedSamplePitch(DestinationAllocation, Width, Height, 0);
    if (DestinationPitch < Width * sizeof(ULONG) || DestinationAllocation->Size / Height < DestinationPitch)
        return;
    if (NT_SUCCESS(DxgkVidMmMapAllocationCpu(DestinationAllocation, &DestinationVa)))
    {
        DxgkpTraceTrackedSurfaceSample("dst", Entry->RefreshPresentId, Entry->SubmissionFenceId, &Entry->RefreshDstRect, DestinationVa, DestinationPitch, Width, Height);
    }

    if (Entry->SourceAllocationHandle == NULL)
        return;

    SourceAllocation = Entry->SourceAllocation;
    if (SourceAllocation == NULL || SourceAllocation->Adapter != Adapter)
        return;

    Width = Entry->SourceWidth;
    Height = Entry->SourceHeight;
    SourcePitch = Entry->SourcePitch;

    if (Width == 0 || Height == 0 || Width > (MAXULONG / sizeof(ULONG)))
        return;

    SourcePitch = DxgkpTrackedSamplePitch(SourceAllocation, Width, Height, SourcePitch);
    if (SourcePitch < Width * sizeof(ULONG) || SourceAllocation->Size / Height < SourcePitch)
        return;
    if (NT_SUCCESS(DxgkVidMmMapAllocationCpu(SourceAllocation, &SourceVa)))
    {
        DxgkpTraceTrackedSurfaceSample("src", Entry->RefreshPresentId, Entry->SubmissionFenceId, &Entry->RefreshDstRect, SourceVa, SourcePitch, Width, Height);
    }
}

static NTSTATUS
DxgkpProgramTrackedScanout(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry)
{
    PDXGKVMM_ALLOCATION Allocation;
    LARGE_INTEGER PrimaryAddress;
    HANDLE AllocationHandle;
    BOOLEAN SharedPrimaryRefresh;
    NTSTATUS Status;

    if (Adapter == NULL ||
        Entry == NULL ||
        (!Entry->RefreshSharedPrimaryOnRetire &&
         !Entry->ProgramSourceScanoutOnRetire) ||
        (Entry->RefreshSharedPrimaryOnRetire &&
         Entry->ProgramSourceScanoutOnRetire) ||
        !Entry->SharedSurfaceRundownHeld ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SharedPrimaryRefresh =
        Entry->RefreshSharedPrimaryOnRetire;
    if (SharedPrimaryRefresh)
    {
        Allocation = Entry->RefreshAllocation;
        AllocationHandle = Entry->RefreshAllocationHandle;
    }
    else
    {
        Allocation = Entry->SourceAllocation;
        AllocationHandle = Entry->SourceAllocationHandle;
    }

    if (AllocationHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Allocation == NULL || Allocation->Adapter != Adapter)
    {
        DXGKRNL_WARN("DxgkpProgramTrackedScanout: invalid allocation %p\n",
                     AllocationHandle);
        return STATUS_INVALID_PARAMETER;
    }

    ASSERT(Adapter->SharedSurfaceGeneration == Entry->SharedSurfaceGeneration);

    if (SharedPrimaryRefresh)
        DxgkpTraceTrackedRefreshSamples(Adapter, Entry, Allocation);

    PrimaryAddress = DxgkVidMmGetAllocationPrimaryAddress(Allocation);
    Status = DxgkpProgramSharedPrimaryScanout(Adapter, Allocation, Entry->RefreshVidPnSourceId, (D3DKMT_HANDLE)(ULONG_PTR)AllocationHandle, Entry->RefreshPresentId);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpProgramTrackedScanout: SetVidPnSourceAddress failed 0x%08lX fence=%u alloc=%p seg=%u addr=0x%I64x\n", Status, Entry->SubmissionFenceId, AllocationHandle, Allocation->SegmentId, PrimaryAddress.QuadPart);
        goto Cleanup;
    }

    if (InterlockedIncrement(&DxgkpTrackedRefreshTraceCount) <= 128)
    {
        DXGKRNL_TRACE("DxgkpProgramTrackedScanout: fence=%u "
                      "present=%llu alloc=%p seg=%u addr=0x%I64x src=%u status=0x%08lX\n",
                      Entry->SubmissionFenceId,
                      Entry->RefreshPresentId,
                      AllocationHandle,
                      Allocation->SegmentId,
                      PrimaryAddress.QuadPart,
                      Entry->RefreshVidPnSourceId,
                      Status);
    }

Cleanup:
    return Status;
}

static VOID
DxgkpFreeTrackedDmaBufferEntry(
    _In_opt_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry,
    _In_ BOOLEAN Completed,
    _In_ BOOLEAN FreeDmaBuffer,
    _In_ BOOLEAN MiniportCallbacksValid)
{
    BOOLEAN DeviceWorkOwned;
    BOOLEAN ExternalCleanupOwned;
    BOOLEAN WakeOrderedWaits;
    NTSTATUS PresentStatus = STATUS_DEVICE_REMOVED;

    WakeOrderedWaits = Completed && Entry->SignalSyncObjectReference != NULL;
    if (Completed)
        DxgkTrackedWorkCoreRetire(&Entry->TrackedWork);
    else
        DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);

    /*
     * Retirement publishes CPU-backed monitored fences.  Wake ordered waits
     * only after that publication, while this entry still owns its device
     * reference, so a retried wait cannot observe the old fence value and
     * park without another wakeup.
     */
    if (WakeOrderedWaits)
        DxgkContextOrderWakeDevice(Entry->Device);

    DeviceWorkOwned = DxgkTrackedWorkCoreOwnsDeviceWork(&Entry->TrackedWork);
    ExternalCleanupOwned = DxgkTrackedWorkCoreOwnsExternalCleanup(&Entry->TrackedWork);
    if (Entry->FenceIdentityOwned && Adapter != NULL)
        DxgkReleaseSubmittedFenceIdentity(Adapter, Entry->NodeOrdinal, Entry->SubmissionFenceId, Entry->FenceIdentityEpoch);
    if (Completed &&
        MiniportCallbacksValid &&
        (Entry->RefreshSharedPrimaryOnRetire ||
         Entry->ProgramSourceScanoutOnRetire))
    {
        PresentStatus = DxgkpProgramTrackedScanout(Adapter, Entry);
    }

    if (DeviceWorkOwned &&
        (Entry->RefreshSharedPrimaryOnRetire ||
         Entry->ProgramSourceScanoutOnRetire))
    {
        DxgkDeviceWorkCompleteWithStatus(Entry->DeviceWork, PresentStatus);
    }

    /* GPU completion drives the retained monitored fence even when user mode
     * destroyed its public handle after submission. */
    DxgkSyncObjectReleaseTrackedSignal(Entry->SignalSyncObjectReference, FALSE, Entry->SignalFenceValue);
    Entry->SignalSyncObjectReference = NULL;

    if (Entry->PresentBindingReferenceList != NULL)
    {
        UINT Index;

        for (Index = 0; Index < Entry->PresentBindingReferenceCount; ++Index)
        {
            if (ExternalCleanupOwned)
            {
                NTSTATUS Status = DxgkVidMmDestroyPresentBinding(Entry->Device, Entry->PresentBindingReferenceList[Index]);

                if (!NT_SUCCESS(Status))
                    DXGKRNL_WARN("DxgkpFreeTrackedDmaBufferEntry: present binding close deferred in VidMm 0x%08lX\n", Status);
            }
            else
                DxgkVidMmDereferenceLogicalAllocation(Entry->PresentBindingReferenceList[Index]);
        }
        ExFreePoolWithTag(Entry->PresentBindingReferenceList, TAG_DXGK_SUBMITDMA);
    }
    if (Entry->OpenBindingReferenceList != NULL)
    {
        UINT Index;

        for (Index = 0; Index < Entry->OpenBindingReferenceCount; ++Index)
            DxgkVidMmDereferenceLogicalAllocation(Entry->OpenBindingReferenceList[Index]);
        ExFreePoolWithTag(Entry->OpenBindingReferenceList, TAG_DXGK_SUBMITDMA);
    }
    if (Entry->AllocationReferenceList != NULL)
    {
        UINT Index;

        for (Index = 0; Index < Entry->AllocationReferenceCount; ++Index)
        {
            DxgkVidMmReleaseSubmissionResidencyPin(Entry->AllocationReferenceList[Index]);
            DxgkVidMmDereferenceAllocation(Entry->AllocationReferenceList[Index]);
        }
        ExFreePoolWithTag(Entry->AllocationReferenceList, TAG_DXGK_SUBMITDMA);
    }
    if (Entry->LifetimeAllocationReferenceList != NULL)
    {
        UINT Index;

        for (Index = 0;
             Index < Entry->LifetimeAllocationReferenceCount;
             ++Index)
        {
            DxgkVidMmDereferenceAllocation(
                Entry->LifetimeAllocationReferenceList[Index]);
        }
        ExFreePoolWithTag(Entry->LifetimeAllocationReferenceList,
                          TAG_DXGK_SUBMITDMA);
    }

    if (FreeDmaBuffer)
        DxgkFreeDmaBuffer(Entry->DmaBuffer);
    if (Entry->SourceAllocation != NULL)
        DxgkVidMmDereferenceAllocation(Entry->SourceAllocation);
    if (Entry->RefreshAllocation != NULL)
        DxgkVidMmDereferenceAllocation(Entry->RefreshAllocation);
    if (Entry->SourceOpenBindingReference != NULL)
        DxgkVidMmDereferenceLogicalAllocation(Entry->SourceOpenBindingReference);
    if (Entry->DestinationOpenBindingReference != NULL)
        DxgkVidMmDereferenceLogicalAllocation(Entry->DestinationOpenBindingReference);
    if (Entry->SharedSurfaceRundownHeld && Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->SharedSurfaceRundown);
    if (Entry->Context != NULL)
        DxgkDereferenceContext(Entry->Context);
    if (Entry->Device != NULL && Entry->Device->ProcessRecord != NULL)
        (VOID)DxgkSubmissionAccountingRelease(&Entry->SubmissionAccounting, &Entry->Device->InFlightSubmissions, &Entry->Device->ProcessRecord->InFlightSubmissions);
    if (DeviceWorkOwned)
        DxgkDeviceWorkDestroy(Entry->DeviceWork);
    Entry->DeviceWork = NULL;
    if (Entry->Device != NULL)
        DxgkDereferenceDevice(Entry->Device);
    ExFreePoolWithTag(Entry, TAG_DXGK_SUBMITDMA);
}

static VOID NTAPI
DxgkpRetireSubmittedDmaBuffersWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = Context;
    LIST_ENTRY FreeList;
    KIRQL OldIrql;
    LONG ActiveWorkers;
    ULONG Batch;

    if (Adapter == NULL)
        return;

    for (;;)
    {
        InitializeListHead(&FreeList);
        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        for (Batch = 0; Batch < 64 && !IsListEmpty(&Adapter->SubmitDmaRetireListHead); Batch++)
        {
            PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaRetireListHead);

            InsertTailList(&FreeList, Link);
        }
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

        while (!IsListEmpty(&FreeList))
        {
            PDXGKRNL_SUBMIT_DMA_BUFFER Entry = CONTAINING_RECORD(RemoveHeadList(&FreeList), DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            BOOLEAN Completed = Entry->CleanupAsCompleted;
#else
            BOOLEAN Completed = TRUE;
#endif

            DxgkpFreeTrackedDmaBufferEntry(
                Adapter,
                Entry,
                Completed,
                TRUE,
                Adapter->MiniportDeviceContext != NULL &&
                    (Adapter->State == DxgkAdapterStateStarted ||
                     Adapter->State == DxgkAdapterStateStopping));
        }

        KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
        if (!IsListEmpty(&Adapter->SubmitDmaRetireListHead))
        {
            KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
            continue;
        }
        InterlockedExchange(&Adapter->SubmitDmaRetireWorkQueued, 0);
        ActiveWorkers = InterlockedDecrement(&Adapter->SubmitDmaRetireActiveWorkers);
        ASSERT(ActiveWorkers == 0);
        KeSetEvent(&Adapter->SubmitDmaRetireDrainedEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);
        return;
    }
}

VOID
NTAPI
DxgkRetireCompletedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY RetireList;
    KIRQL OldIrql;
    LONG ActiveWorkers;
    ULONG CompletedFenceId;
    BOOLEAN PendingRetire;
    BOOLEAN QueueWorker = FALSE;

    if (Adapter == NULL)
        return;

    CompletedFenceId = Adapter->LastCompletedSubmissionFenceId;
    InitializeListHead(&RetireList);

    /*
     * Walk the whole list: independent GPU nodes complete out of global
     * fence order, so an unreached entry no longer implies everything
     * behind it is unreached. Each entry retires only against its node's
     * completed fence; the adapter-global maximum cannot prove completion on
     * any particular node.
     */
    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    {
        PLIST_ENTRY Link = Adapter->SubmitDmaListHead.Flink;

        while (Link != &Adapter->SubmitDmaListHead)
        {
            PDXGKRNL_SUBMIT_DMA_BUFFER Entry = CONTAINING_RECORD(Link, DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
            PLIST_ENTRY Next = Link->Flink;
            ULONG NodeFence = Adapter->NodeLastCompletedFenceId[Entry->NodeOrdinal];

            if (NodeFence != 0 && DxgkpFenceIdReached(NodeFence, Entry->SubmissionFenceId))
            {
                if (DxgkTrackedWorkCoreRetire(&Entry->TrackedWork))
                {
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
                    Entry->CleanupAsCompleted = TRUE;
#endif
                    RemoveEntryList(Link);
                    InsertTailList(&RetireList, Link);
                }
                else
                {
                    ASSERT(FALSE);
                }
            }

            Link = Next;
        }
    }

    while (!IsListEmpty(&RetireList))
    {
        PLIST_ENTRY Link = RemoveHeadList(&RetireList);
        InsertTailList(&Adapter->SubmitDmaRetireListHead, Link);
    }

    if (!IsListEmpty(&Adapter->SubmitDmaRetireListHead) && InterlockedIncrement(&DxgkpRetireTraceCount) <= 128)
    {
        DXGKRNL_TRACE("DxgkRetireCompletedDmaBuffers: completedFence=%u "
                      "queued retire work state=%d\n",
                      CompletedFenceId,
                      Adapter->State);
    }

    PendingRetire = !IsListEmpty(&Adapter->SubmitDmaRetireListHead);
    if (PendingRetire)
        KeClearEvent(&Adapter->SubmitDmaRetireDrainedEvent);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    if (!PendingRetire)
        return;

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    if (!IsListEmpty(&Adapter->SubmitDmaRetireListHead) && InterlockedCompareExchange(&Adapter->SubmitDmaRetireWorkQueued, 1, 0) == 0)
    {
        ActiveWorkers = InterlockedIncrement(&Adapter->SubmitDmaRetireActiveWorkers);
        ASSERT(ActiveWorkers == 1);
        QueueWorker = TRUE;
    }
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    if (QueueWorker)
        ExQueueWorkItem(&Adapter->SubmitDmaRetireWorkItem, DelayedWorkQueue);
}

VOID
NTAPI
DxgkReleaseTrackedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN MiniportCallbacksValid)
{
    LIST_ENTRY CancelList;
    LIST_ENTRY CompletedList;
    KIRQL OldIrql;

    if (Adapter == NULL)
        return;

    if (InterlockedCompareExchange(&Adapter->SubmitDmaRetireActiveWorkers, 0, 0) != 0)
        KeWaitForSingleObject(&Adapter->SubmitDmaRetireDrainedEvent, Executive, KernelMode, FALSE, NULL);

    InitializeListHead(&CancelList);
    InitializeListHead(&CompletedList);

    KeAcquireSpinLock(&Adapter->SubmitDmaLock, &OldIrql);
    while (!IsListEmpty(&Adapter->SubmitDmaListHead))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaListHead);
        PDXGKRNL_SUBMIT_DMA_BUFFER Entry = CONTAINING_RECORD(Link, DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
        BOOLEAN Cancelled;

        Cancelled = DxgkTrackedWorkCoreCancel(&Entry->TrackedWork);
        ASSERT(Cancelled);
        InsertTailList(&CancelList, Link);
    }
    while (!IsListEmpty(&Adapter->SubmitDmaRetireListHead))
    {
        PLIST_ENTRY Link = RemoveHeadList(&Adapter->SubmitDmaRetireListHead);
        InsertTailList(&CompletedList, Link);
    }
    if (InterlockedCompareExchange(&Adapter->SubmitDmaRetireActiveWorkers, 0, 0) == 0)
        KeSetEvent(&Adapter->SubmitDmaRetireDrainedEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Adapter->SubmitDmaLock, OldIrql);

    while (!IsListEmpty(&CompletedList))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Entry;

        Entry = CONTAINING_RECORD(RemoveHeadList(&CompletedList), DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
        DxgkpFreeTrackedDmaBufferEntry(
            Adapter,
            Entry,
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            Entry->CleanupAsCompleted,
#else
            TRUE,
#endif
            TRUE,
            MiniportCallbacksValid);
    }
    while (!IsListEmpty(&CancelList))
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Entry;

        Entry = CONTAINING_RECORD(RemoveHeadList(&CancelList), DXGKRNL_SUBMIT_DMA_BUFFER, ListEntry);
        DxgkpFreeTrackedDmaBufferEntry(Adapter, Entry, FALSE, TRUE, MiniportCallbacksValid);
    }
}

/* ========================================================================
 * Private helpers
 * ====================================================================== */

/*
 * DxgkpEnsureGlobalInitialization
 *
 * Performs one-time global initialisation.  Called from DxgkInitializeEx
 * (and optionally from DriverEntry if dxgkrnl loads as a service).
 *
 * CRITICAL: When dxgkrnl is loaded as an import dependency of a miniport
 * driver (e.g. kmdod matched by CDD), DriverEntry is NOT called by the
 * I/O manager.  The PE loader loads the image and resolves exports but
 * skips DriverEntry.  All global initialization MUST happen here, which
 * is guaranteed to run before any dxgkrnl function is used.
 *
 * Safe to call from multiple simultaneous threads; only the first caller
 * performs the actual work.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkpEnsureGlobalInitialization(VOID)
{
    LONG PreviousState;
    NTSTATUS Status;

    PreviousState = InterlockedCompareExchange(&DxgkpInitialized, 1, 0);
    if (PreviousState != 0)
    {
        LARGE_INTEGER Delay;

        Delay.QuadPart = -10 * 1000;
        while ((PreviousState = InterlockedCompareExchange(&DxgkpInitialized, 0, 0)) == 1)
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        ASSERT(PreviousState == 2 || PreviousState == 3);
        return DxgkpInitializationStatus;
    }

    /* Initialize global adapter list and lock. */
    KeInitializeSpinLock(&DxgkAdapterGlobalListLock);
    InitializeListHead(&DxgkAdapterGlobalListHead);
    KeInitializeSpinLock(&g_PostDisplayOwnerLock);
    KeInitializeMutex(&g_PostDisplayOwnershipMutex, 0);
    KeInitializeMutex(&g_MiniportRegistrationMutex, 0);
    ExInitializeFastMutex(&DxgkpMapMemoryMutex);
    InitializeListHead(&DxgkpMapMemoryList);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    ExInitializeFastMutex(&DxgkpCallbackMemoryMutex);
    InitializeListHead(&DxgkpCallbackMemoryList);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    ExInitializeFastMutex(&DxgkpPhysicalMemoryMutex);
    InitializeListHead(&DxgkpPhysicalMemoryList);
#endif

    DxgkpInitializeCoreInterface();

    /* Initialize debug helpers. */
    DxgkDebugInit();

    Status = DxgkpMms2Initialize();
    if (!NT_SUCCESS(Status))
    {
        DxgkpInitializationStatus = Status;
        InterlockedExchange(&DxgkpInitialized, 3);
        DXGKRNL_ERR("DxgkpEnsureGlobalInitialization: dxgmms2 registration failed 0x%08lX\n", Status);
        return Status;
    }

    /* Seed D3DKMT handle cookie. */
    Status = DxgkContextInit();
    if (!NT_SUCCESS(Status))
    {
        NTSTATUS RollbackStatus;

        RollbackStatus = DxgkpMms2Uninitialize();
        if (!NT_SUCCESS(RollbackStatus))
            DXGKRNL_ERR("DxgkpEnsureGlobalInitialization: dxgmms2 rollback failed 0x%08lX\n", RollbackStatus);
        DxgkpInitializationStatus = Status;
        InterlockedExchange(&DxgkpInitialized, 3);
        DXGKRNL_ERR("DxgkpEnsureGlobalInitialization: DxgkContextInit failed 0x%08lX\n", Status);
        return Status;
    }

    /* Bring the panic screen up through the display owner (see above). */
    DxgkpRegisterBugCheckCallback();

    DxgkpInitializationStatus = STATUS_SUCCESS;
    InterlockedExchange(&DxgkpInitialized, 2);
    DXGKRNL_TRACE("DxgkpEnsureGlobalInitialization: one-time init complete\n");
    return STATUS_SUCCESS;
}

/*
 * DxgkpForwardIrp
 *
 * Skip the current IRP stack location and forward the IRP to the next
 * lower driver in the stack synchronously.
 *
 * IRQL: PASSIVE_LEVEL
 */
static NTSTATUS
DxgkpForwardIrp(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PIRP             Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Adapter->LowerDeviceObject, Irp);
}

static BOOLEAN
DxgkpAcquireVidSchCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (InterlockedCompareExchange(&Adapter->VidSchStopping, 0, 0) != 0)
        return FALSE;
    InterlockedIncrement(&Adapter->VidSchActiveCalls);
    if (InterlockedCompareExchange(&Adapter->VidSchStopping, 0, 0) != 0)
    {
        InterlockedDecrement(&Adapter->VidSchActiveCalls);
        return FALSE;
    }
    return TRUE;
}

static VOID
DxgkpReleaseVidSchCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG ActiveCalls = InterlockedDecrement(&Adapter->VidSchActiveCalls);

    ASSERT(ActiveCalls >= 0);
}

static VOID
DxgkpWaitForVidSchCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -10000;
    while (InterlockedCompareExchange(&Adapter->VidSchActiveCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

VOID
NTAPI
DxgkDrainVidSchCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter != NULL)
        DxgkpWaitForVidSchCallbacks(Adapter);
}

static VOID
DxgkpDisablePeriodicInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter);

static VOID
DxgkpDisconnectAdapterInterrupt(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    DxgkBlockInterruptCallbacks(Adapter);
    DxgkpDisablePeriodicInterruptHandoff(Adapter);
    if (Adapter->InterruptMessageTable != NULL)
    {
        IO_DISCONNECT_INTERRUPT_PARAMETERS DisconnectParams;

        RtlZeroMemory(&DisconnectParams, sizeof(DisconnectParams));
        DisconnectParams.Version = CONNECT_MESSAGE_BASED;
        DisconnectParams.ConnectionContext.InterruptMessageTable = Adapter->InterruptMessageTable;
        IoDisconnectInterruptEx(&DisconnectParams);
        Adapter->InterruptMessageTable = NULL;
        Adapter->InterruptObject = NULL;
    }
    else if (Adapter->InterruptObject != NULL)
    {
        IoDisconnectInterrupt(Adapter->InterruptObject);
        Adapter->InterruptObject = NULL;
    }
}

/*
 * InterruptLock is also taken by DxgkCbNotifyInterrupt at the device's
 * synchronize IRQL.  Non-ISR users must raise to at least that IRQL before
 * acquiring it; KeAcquireSpinLock would raise only to DISPATCH_LEVEL.
 */
static KIRQL
DxgkpAcquireAdapterInterruptLock(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL CurrentIrql;
    KIRQL LockIrql;
    KIRQL OldIrql;

    CurrentIrql = KeGetCurrentIrql();
    LockIrql = Adapter->InterruptLevel;
    if (LockIrql < DISPATCH_LEVEL)
        LockIrql = DISPATCH_LEVEL;

    OldIrql = CurrentIrql;
    if (CurrentIrql < LockIrql)
        KeRaiseIrql(LockIrql, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&Adapter->InterruptLock);
    return OldIrql;
}

static VOID
DxgkpReleaseAdapterInterruptLock(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ KIRQL OldIrql)
{
    KIRQL LockIrql = KeGetCurrentIrql();

    KeReleaseSpinLockFromDpcLevel(&Adapter->InterruptLock);
    if (LockIrql != OldIrql)
        KeLowerIrql(OldIrql);
}

static BOOLEAN
DxgkpPeriodicInterruptHandoffSupported(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    return Adapter != NULL &&
           REACTOS_WDDM_TARGET_LEVEL >=
               DXGK_CAPS_CORE_LEVEL_WDDM_2_2 &&
           Adapter->MiniportContext != NULL &&
           !Adapter->MiniportContext->UseDodLayout &&
           DxgkCapsCoreInterfaceVersionAtLeast(
               Adapter->MiniportContext->InitData.s.Version,
               DXGK_CAPS_CORE_LEVEL_WDDM_2_2) &&
           DXGK_CB_FULL(
               Adapter,
               DxgkDdiCreatePeriodicFrameNotification) != NULL &&
           DXGK_CB_FULL(
               Adapter,
               DxgkDdiDestroyPeriodicFrameNotification) != NULL;
#else
    UNREFERENCED_PARAMETER(Adapter);
    return FALSE;
#endif
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
static BOOLEAN
DxgkpMonitoredFenceInterruptSupported(
    _In_opt_ PDXGKRNL_ADAPTER Adapter)
{
    return Adapter != NULL &&
           Adapter->MiniportContext != NULL &&
           !Adapter->MiniportContext->UseDodLayout &&
           DxgkMonitoredInterruptCoreSupported(
               REACTOS_WDDM_TARGET_LEVEL,
               DxgkCapsCoreInterfaceVersionToLevel(
                   Adapter->MiniportContext->InitData.s.Version),
               Adapter->NodeCount);
}

static BOOLEAN
DxgkpQueueMonitoredFenceEvaluation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG EngineOrdinal)
{
    if (!DxgkpMonitoredFenceInterruptSupported(Adapter) ||
        NodeOrdinal >= Adapter->NodeCount ||
        EngineOrdinal != 0)
    {
        return FALSE;
    }

    /*
     * Publish the engine's fence write before making its node visible to
     * the adapter DPC.  The DPC pairs this with the acquire barrier in the
     * monitored-fence registry evaluator.
     */
    KeMemoryBarrier();
    return NT_SUCCESS(
        DxgkMonitoredInterruptCoreEnqueue(
            &Adapter->MonitoredFencePendingNodes,
            Adapter->NodeCount,
            NodeOrdinal,
            EngineOrdinal));
}

static VOID
DxgkpDrainMonitoredFenceInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG PendingNodes;
    ULONG NodeOrdinal;

    PendingNodes =
        DxgkMonitoredInterruptCoreDrain(
            &Adapter->MonitoredFencePendingNodes);
    for (NodeOrdinal = 0; NodeOrdinal < 32; ++NodeOrdinal)
    {
        NTSTATUS Status;

        if ((PendingNodes & (1UL << NodeOrdinal)) == 0)
            continue;
        Status =
            DxgkSyncNotifyMonitoredFence(
                Adapter,
                NodeOrdinal,
                0);
        if (!NT_SUCCESS(Status) &&
            Status != STATUS_NOT_FOUND &&
            Status != STATUS_DELETE_PENDING)
        {
            DXGKRNL_WARN(
                "DXGKRNL: monitored-fence interrupt for node %lu "
                "was rejected (0x%08lX)\n",
                NodeOrdinal,
                Status);
        }
    }
}
#endif

static VOID
DxgkpEnablePeriodicInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    OldIrql = DxgkpAcquireAdapterInterruptLock(Adapter);
    if (DxgkpPeriodicInterruptHandoffSupported(Adapter))
    {
        DxgkPeriodicInterruptCoreEnableLocked(
            &Adapter->PeriodicInterruptCore);
    }
    else
    {
        DxgkPeriodicInterruptCoreDisableLocked(
            &Adapter->PeriodicInterruptCore);
    }
    Adapter->PeriodicInterruptOverflowReported = FALSE;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    InterlockedExchange(
        &Adapter->MonitoredFencePendingNodes,
        0);
#endif
    DxgkpReleaseAdapterInterruptLock(Adapter, OldIrql);
}

static VOID
DxgkpDisablePeriodicInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    OldIrql = DxgkpAcquireAdapterInterruptLock(Adapter);
    DxgkPeriodicInterruptCoreDisableLocked(
        &Adapter->PeriodicInterruptCore);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    InterlockedExchange(
        &Adapter->MonitoredFencePendingNodes,
        0);
#endif
    DxgkpReleaseAdapterInterruptLock(Adapter, OldIrql);
}

/*
 * Drain the fixed ISR handoff without carrying InterruptLock into the sync
 * registry.  The empty transition and DpcActive update happen under the same
 * lock as enqueue, so a later ISR either joins this drain or queues a new one.
 */
static VOID
DxgkpDrainPeriodicInterruptHandoff(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    for (;;)
    {
        DXGK_PERIODIC_INTERRUPT_CORE_ENTRY Entry;
        ULONGLONG OverflowCount = 0;
        BOOLEAN HaveEntry;
        BOOLEAN ReportOverflow = FALSE;
        KIRQL OldIrql;
        NTSTATUS Status;

        OldIrql = DxgkpAcquireAdapterInterruptLock(Adapter);
        HaveEntry = DxgkPeriodicInterruptCoreDequeueLocked(
            &Adapter->PeriodicInterruptCore,
            &Entry);
        if (!HaveEntry &&
            Adapter->PeriodicInterruptCore.State ==
                DxgkPeriodicInterruptOverflowed &&
            !Adapter->PeriodicInterruptOverflowReported)
        {
            Adapter->PeriodicInterruptOverflowReported = TRUE;
            OverflowCount =
                Adapter->PeriodicInterruptCore.OverflowCount;
            ReportOverflow = TRUE;
        }
        DxgkpReleaseAdapterInterruptLock(Adapter, OldIrql);

        if (ReportOverflow)
        {
            DXGKRNL_ERR(
                "DXGKRNL: periodic interrupt handoff disabled after "
                "%I64u protocol/overflow failure(s)\n",
                OverflowCount);
        }
        if (!HaveEntry)
            break;

        Status = DxgkSyncNotifyPeriodicFenceCount(
            Adapter,
            Entry.VidPnTargetId,
            Entry.NotificationId,
            Entry.PendingCount);
        if (!NT_SUCCESS(Status) &&
            Status != STATUS_NOT_FOUND &&
            Status != STATUS_DELETE_PENDING)
        {
            DXGKRNL_WARN(
                "DXGKRNL: periodic notification %lu target %lu "
                "count %I64u was rejected (0x%08lX)\n",
                Entry.NotificationId,
                Entry.VidPnTargetId,
                Entry.PendingCount,
                Status);
        }
    }
}

/*
 * DxgkpAdapterDpcRoutine
 *
 * KDPC callback.  Invoked at DISPATCH_LEVEL by the I/O manager after the
 * ISR requests a DPC.  Calls the miniport's DxgkDdiDpcRoutine.
 *
 * IRQL: DISPATCH_LEVEL
 */
static VOID
NTAPI
DxgkpAdapterDpcRoutine(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)DeferredContext;
    LONG             Sequence;
    BOOLEAN          Logged;
    ULONGLONG        Start100ns;
    ULONGLONG        ElapsedUs = 0;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Adapter == NULL)
        return;
    if (!DxgkpAcquireVidSchCallback(Adapter))
        return;

    Sequence = InterlockedIncrement(&Adapter->DpcCount);
    Logged = (Sequence <= DXGK_TRACE_DPC_LOG_LIMIT);

    if (Logged)
    {
        DXGKRNL_TRACE("DxgkpAdapterDpcRoutine: seq=%ld state=%d irq=%ld queue=%ld t+%I64u us\n",
                      Sequence,
                      Adapter->State,
                      Adapter->InterruptCount,
                      Adapter->QueueDpcCount,
                      DxgkpTraceSinceStartUs(Adapter));
    }

    if (Adapter->MiniportContext->InitData.s.DxgkDdiDpcRoutine != NULL && DxgkAcquireInterruptCallback(Adapter))
    {
        Start100ns = DxgkpTraceNow100ns();
        Adapter->MiniportContext->InitData.s.DxgkDdiDpcRoutine(Adapter->MiniportDeviceContext);
        ElapsedUs = DxgkpTraceElapsedUs(Start100ns);
        InterlockedExchange64(
            &Adapter->LastMiniportDpcCompletedCounter,
            KeQueryPerformanceCounter(NULL).QuadPart);
        DxgkReleaseInterruptCallback(Adapter);

        if (Logged || ElapsedUs >= DXGK_TRACE_SLOW_DPC_US)
        {
            DXGKRNL_TRACE("DxgkpAdapterDpcRoutine: seq=%ld done dur=%I64u us irq=%ld queue=%ld t+%I64u us\n",
                          Sequence,
                          ElapsedUs,
                          Adapter->InterruptCount,
                          Adapter->QueueDpcCount,
                          DxgkpTraceSinceStartUs(Adapter));
        }
    }
    else if (Logged)
    {
        DXGKRNL_TRACE("DxgkpAdapterDpcRoutine: seq=%ld no miniport DPC routine\n",
                      Sequence);
    }

    DxgkpDrainPeriodicInterruptHandoff(Adapter);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    DxgkpDrainMonitoredFenceInterruptHandoff(Adapter);
#endif

    /* A target bit maps one-to-one to the implemented source ordinal. */
    {
        ULONG VsyncMask = (ULONG)InterlockedExchange(&Adapter->VsyncPending, 0);
        ULONG SourceId;

        for (SourceId = 0; SourceId < 32; SourceId++)
        {
            if ((VsyncMask & (1UL << SourceId)) != 0)
                DxgkpNotifyVSync(Adapter, (D3DDDI_VIDEO_PRESENT_SOURCE_ID)SourceId);
        }
    }

    DxgkRetireCompletedDmaBuffers(Adapter);
    DxgkpReleaseVidSchCallback(Adapter);
}

/* Forward declarations for callbacks defined later in this file */
static PDXGKRNL_ADAPTER
DxgkpHandleToAdapter(
    _In_ HANDLE DeviceHandle);

NTSTATUS APIENTRY DxgkCbQueryServices(HANDLE, DXGK_SERVICES, PINTERFACE);
NTSTATUS APIENTRY DxgkCbMapMemory(HANDLE, PHYSICAL_ADDRESS, ULONG, BOOLEAN, BOOLEAN, MEMORY_CACHING_TYPE, PVOID*);
NTSTATUS APIENTRY DxgkCbUnmapMemory(HANDLE, PVOID);
BOOLEAN  APIENTRY DxgkCbQueueDpc(HANDLE);
NTSTATUS APIENTRY DxgkCbReadDeviceSpace(HANDLE, ULONG, PVOID, ULONG, ULONG, PULONG);
NTSTATUS APIENTRY DxgkCbWriteDeviceSpace(HANDLE, ULONG, PVOID, ULONG, ULONG, PULONG);

/* ========================================================================
 * DXGK_INTERFACE callbacks supplied to WDDM miniports.
 *
 * A callback with an explicit "Unavailable" suffix is a deliberate failure
 * boundary for a subsystem that ReactOS does not yet provide.  Keep such
 * callbacks deterministic and never turn them into success placeholders.
 * ====================================================================== */

static const GUID DxgkpPciDevicePresentInterfaceGuid =
{
    0xd1b82c26, 0xbf49, 0x45ef,
    {0xb2, 0x16, 0x71, 0xcb, 0xd7, 0x88, 0x9b, 0x57}
};

static const GUID DxgkpBusInterfaceStandardGuid =
{
    0x496b8280, 0x6f25, 0x11d0,
    {0xbe, 0xaf, 0x08, 0x00, 0x2b, 0xe2, 0x09, 0x2f}
};

static NTSTATUS
DxgkpQueryPdoInterface(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ const GUID *InterfaceType,
    _In_ USHORT Size,
    _In_ USHORT Version,
    _Out_writes_bytes_(Size) PINTERFACE Interface)
{
    PIO_STACK_LOCATION Stack;
    PDEVICE_OBJECT TargetDevice;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    PAGED_CODE();

    if (PhysicalDeviceObject == NULL || InterfaceType == NULL || Interface == NULL || Size < sizeof(INTERFACE))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Interface, Size);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    TargetDevice = IoGetAttachedDeviceReference(PhysicalDeviceObject);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, TargetDevice, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
    {
        ObDereferenceObject(TargetDevice);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = InterfaceType;
    Stack->Parameters.QueryInterface.Size = Size;
    Stack->Parameters.QueryInterface.Version = Version;
    Stack->Parameters.QueryInterface.Interface = Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(TargetDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    ObDereferenceObject(TargetDevice);
    return Status;
}

static VOID
DxgkpReleasePciBusInterface(
    _Inout_ PDXGKRNL_ADAPTER Adapter)
{
    if (!Adapter->PciBusInterfaceValid)
        return;

    Adapter->PciBusInterfaceValid = FALSE;
    if (Adapter->PciBusInterface.InterfaceDereference != NULL)
    {
        Adapter->PciBusInterface.InterfaceDereference(
            Adapter->PciBusInterface.Context);
    }
    RtlZeroMemory(&Adapter->PciBusInterface,
                  sizeof(Adapter->PciBusInterface));
}

static NTSTATUS
DxgkpCapturePciBusInterface(
    _Inout_ PDXGKRNL_ADAPTER Adapter)
{
    BUS_INTERFACE_STANDARD BusInterface;
    NTSTATUS Status;

    ASSERT(!Adapter->PciBusInterfaceValid);
    Status = DxgkpQueryPdoInterface(
                 Adapter->PhysicalDeviceObject,
                &DxgkpBusInterfaceStandardGuid,
                 sizeof(BusInterface),
                 1,
                (PINTERFACE)&BusInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    if (BusInterface.Size < sizeof(BusInterface) ||
        BusInterface.Version < 1 ||
        BusInterface.Context == NULL ||
        BusInterface.GetBusData == NULL ||
        BusInterface.SetBusData == NULL ||
        BusInterface.InterfaceDereference == NULL)
    {
        if (BusInterface.InterfaceDereference != NULL)
            BusInterface.InterfaceDereference(BusInterface.Context);
        return STATUS_NOT_SUPPORTED;
    }

    Adapter->PciBusInterface = BusInterface;
    Adapter->PciBusInterfaceValid = TRUE;
    return STATUS_SUCCESS;
}

/*
 * DxgkCbIsDevicePresent — offset 0x60
 * The callback reports presence through its output parameter; the return
 * value reports whether the query itself was accepted.
 */
static NTSTATUS
APIENTRY
DxgkCbIsDevicePresent(
    _In_ HANDLE DeviceHandle,
    _In_ PPCI_DEVICE_PRESENCE_PARAMETERS DevicePresenceParameters,
    _Out_ PBOOLEAN DevicePresent)
{
    PDXGKRNL_ADAPTER Adapter;
    PCI_DEVICE_PRESENT_INTERFACE PresentInterface;
    NTSTATUS Status;

    PAGED_CODE();

    if (DevicePresent == NULL)
        return STATUS_INVALID_PARAMETER;

    *DevicePresent = FALSE;

    if (DevicePresenceParameters == NULL ||
        DevicePresenceParameters->Size != sizeof(*DevicePresenceParameters) ||
        (DevicePresenceParameters->Flags & ~(PCI_USE_SUBSYSTEM_IDS |
                                              PCI_USE_REVISION |
                                              PCI_USE_VENDEV_IDS |
                                              PCI_USE_CLASS_SUBCLASS |
                                              PCI_USE_PROGIF |
                                              PCI_USE_LOCAL_BUS |
                                              PCI_USE_LOCAL_DEVICE)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (Adapter->PhysicalDeviceObject == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_DEVICE_NOT_READY;
    }

    Status = DxgkpQueryPdoInterface(Adapter->PhysicalDeviceObject, &DxgkpPciDevicePresentInterfaceGuid, sizeof(PresentInterface), PCI_DEVICE_PRESENT_INTERFACE_VERSION, (PINTERFACE)&PresentInterface);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return Status;
    }

    if (PresentInterface.Size < sizeof(PresentInterface) ||
        PresentInterface.Version < PCI_DEVICE_PRESENT_INTERFACE_VERSION ||
        PresentInterface.IsDevicePresentEx == NULL)
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        *DevicePresent = PresentInterface.IsDevicePresentEx(PresentInterface.Context, DevicePresenceParameters);
        Status = STATUS_SUCCESS;
    }

    if (PresentInterface.InterfaceDereference != NULL)
        PresentInterface.InterfaceDereference(PresentInterface.Context);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

static PVOID
APIENTRY
DxgkCbGetHandleData(
    _In_ CONST DXGKARGCB_GETHANDLEDATA *HandleData)
{
    if (HandleData == NULL || HandleData->Flags.Reserved != 0)
        return NULL;

    return DxgkVidMmGetHandleData(HandleData->Type, HandleData->hObject, HandleData->Flags.DeviceSpecific);
}

static PVOID
APIENTRY
DxgkCbAcquireHandleData(
    _In_ CONST DXGKARGCB_GETHANDLEDATA *HandleData,
    _Out_ PDXGKARG_RELEASE_HANDLE ReleaseHandle)
{
    if (ReleaseHandle == NULL)
        return NULL;
    *ReleaseHandle = NULL;
    if (HandleData == NULL || HandleData->Flags.Reserved != 0)
        return NULL;

    return DxgkVidMmAcquireHandleData(HandleData->Type, HandleData->hObject, HandleData->Flags.DeviceSpecific, ReleaseHandle);
}

static VOID
APIENTRY
DxgkCbReleaseHandleData(
    _In_ CONST DXGKARGCB_RELEASEHANDLEDATA HandleData)
{
    if (HandleData.ReleaseHandle != NULL)
        DxgkVidMmReleaseHandleData(HandleData.Type, HandleData.ReleaseHandle);
}

/*
 * DxgkCbGetHandleParent — offset 0x70
 */
static D3DKMT_HANDLE
APIENTRY
DxgkCbGetHandleParent(
    IN_D3DKMT_HANDLE hAllocation)
{
    return DxgkVidMmGetHandleParent(hAllocation);
}

/*
 * DxgkCbEnumHandleChildren — offset 0x78
 */
static D3DKMT_HANDLE
APIENTRY
DxgkCbEnumHandleChildren(
    IN_CONST_PDXGKARGCB_ENUMHANDLECHILDREN EnumHandleChildren)
{
    if (EnumHandleChildren == NULL)
        return 0;

    return DxgkVidMmEnumHandleChildren(
        EnumHandleChildren->hObject,
        EnumHandleChildren->Index);
}

/*
 * VidPN and Monitor interface callbacks (0x90 and 0x98) are now provided by
 * the real implementations in vidpn.c: DxgkCbQueryVidPnInterface and
 * DxgkCbQueryMonitorInterface.  The old stubs have been removed.
 */

/*
 * DxgkCbGetCaptureAddress — offset 0xa0
 */
static NTSTATUS
APIENTRY
DxgkCbGetCaptureAddress(
    INOUT_PDXGKARGCB_GETCAPTUREADDRESS GetCaptureAddress)
{
    return DxgkVidMmGetCaptureAddress(GetCaptureAddress);
}

/*
 * ReactOS has no graphics ETW provider yet.  The WDDM callback has no return
 * value, so retain the ABI slot while deliberately dropping the event.  Do
 * not advertise this as diagnostic parity.
 */
static VOID
APIENTRY
DxgkCbLogEtwEventDisabled(
    _In_ CONST LPCGUID EventGuid,
    _In_ UCHAR Type,
    _In_ USHORT EventBufferSize,
    _In_reads_bytes_(EventBufferSize) PVOID EventBuffer)
{
    UNREFERENCED_PARAMETER(EventGuid);
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(EventBufferSize);
    UNREFERENCED_PARAMETER(EventBuffer);
}

/*
 * Adapter exclusion needs a real scheduler/VidMm access gate and protected
 * callback transaction.  Returning failure is safer than invoking the
 * callback while other adapter access remains possible.
 */
static NTSTATUS
APIENTRY
DxgkCbExcludeAdapterAccessNotSupported(
    _In_ HANDLE DeviceHandle,
    _In_ ULONG  Attributes,
    _In_ DXGKDDI_PROTECTED_CALLBACK DxgkProtectedCallback,
    _In_ PVOID ProtectedCallbackContext)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ProtectedCallbackContext);

    if (DxgkProtectedCallback == NULL ||
        (Attributes & ~(DXGK_EXCLUDE_EVICT_ALL |
                        DXGK_EXCLUDE_CALL_SYNCHRONOUS |
                        DXGK_EXCLUDE_BRIDGE_ACCESS |
                        DXGK_EXCLUDE_EVICT_STANDBY |
                        DXGK_EXCLUDE_EVICT_HIBERNATE |
                        DXGK_EXCLUDE_EVICT_SHUTDOWN |
                        DXGK_EXCLUDE_D3_STATE_TRANSITION |
                        DXGK_EXCLUDE_EVICT_DFX_STANDBY)) != 0 ||
        (Attributes & (DXGK_EXCLUDE_EVICT_ALL |
                       DXGK_EXCLUDE_CALL_SYNCHRONOUS)) ==
            (DXGK_EXCLUDE_EVICT_ALL |
             DXGK_EXCLUDE_CALL_SYNCHRONOUS))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
APIENTRY
DxgkCbCreateContextAllocation(
    INOUT_PDXGKARGCB_CREATECONTEXTALLOCATION ContextAllocation)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context = NULL;
    BOOLEAN ContextReferenced = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    if (ContextAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
    ContextAllocation->hAllocation = NULL;

    if ((ContextAllocation->ContextAllocationFlags.Value & ~0x3U) != 0 ||
        ContextAllocation->hDevice == NULL ||
        ContextAllocation->hDriverAllocation == NULL ||
        ContextAllocation->Size == 0 ||
        ContextAllocation->SupportedSegmentSet == 0 ||
        ContextAllocation->PhysicalAdapterIndex != 0 ||
        (ContextAllocation->Alignment != 0 &&
         (ContextAllocation->Alignment &
          (ContextAllocation->Alignment - 1)) != 0) ||
        (ContextAllocation->ContextAllocationFlags.SharedAcrossContexts &&
         ContextAllocation->hContext != NULL) ||
        (!ContextAllocation->ContextAllocationFlags.SharedAcrossContexts &&
         ContextAllocation->hContext == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(ContextAllocation->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Device = (PDXGKRNL_DEVICE)ContextAllocation->hDevice;
    if (!DxgkReferenceDevice(Device))
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_DELETE_PENDING;
    }
    if (Device->Adapter != Adapter)
    {
        DxgkDereferenceDevice(Device);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_HANDLE;
    }

    if (!ContextAllocation->ContextAllocationFlags.SharedAcrossContexts)
    {
        Context = (PDXGKRNL_CONTEXT)ContextAllocation->hContext;
        ContextReferenced = DxgkReferenceContext(Context);
        if (!ContextReferenced || Context->Device != Device)
        {
            if (ContextReferenced)
                DxgkDereferenceContext(Context);
            DxgkDereferenceDevice(Device);
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
            return STATUS_INVALID_HANDLE;
        }
    }

    Status = DxgkVidMmCreateContextAllocation(
                 Adapter,
                 Device,
                 ContextAllocation->hDriverAllocation,
                 ContextAllocation->Size,
                 ContextAllocation->Alignment,
                 ContextAllocation->SupportedSegmentSet,
                 ContextAllocation->EvictionSegmentSet,
                 ContextAllocation->PreferredSegment,
                 ContextAllocation->HintedBank,
                 ContextAllocation->Flags,
                 ContextAllocation->ContextAllocationFlags.MapGpuVirtualAddress != 0,
                 &ContextAllocation->hAllocation);
    if (NT_SUCCESS(Status) && ContextAllocation->hAllocation != NULL)
    {
        DxgkVidMmTagContextAllocation(ContextAllocation->hAllocation, ContextAllocation->hContext);
        DxgkVidMmDumpContextImages(NULL, ContextAllocation->hAllocation, "create");
    }
    {
        LONG Count = InterlockedIncrement(&Adapter->ContextAllocationCreateCount);

        {
            DXGKRNL_INFO("DxgkCbCreateContextAllocation #%ld seq=#%I64d: size=%Iu flags=0x%x (shared=%u mapva=%u) allocflags=0x%08x (cpuvisible=%u protected=%u cached=%u) segset=0x%x evict=0x%x pref=0x%x align=%u ctx=%p dev=%p -> status=0x%08lx handle=%p va=0x%I64x\n",
                        Count, DxgkDiagSequence(), ContextAllocation->Size, ContextAllocation->ContextAllocationFlags.Value,
                        ContextAllocation->ContextAllocationFlags.SharedAcrossContexts,
                        ContextAllocation->ContextAllocationFlags.MapGpuVirtualAddress,
                        ContextAllocation->Flags.Value, ContextAllocation->Flags.CpuVisible, ContextAllocation->Flags.Protected, ContextAllocation->Flags.Cached,
                        ContextAllocation->SupportedSegmentSet, ContextAllocation->EvictionSegmentSet, ContextAllocation->PreferredSegment.Value, ContextAllocation->Alignment,
                        ContextAllocation->hContext, ContextAllocation->hDevice, Status,
                        ContextAllocation->hAllocation,
                        ContextAllocation->hAllocation != NULL ? *(ULONGLONG *)((PUCHAR)ContextAllocation->hAllocation + 0x8B0) : 0ULL);
        }
    }

    if (ContextReferenced)
        DxgkDereferenceContext(Context);
    DxgkDereferenceDevice(Device);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkCbCreateContextAllocation: resident allocation failed 0x%08lX\n",
                    Status);
    }
    return Status;
}

static NTSTATUS
APIENTRY
DxgkCbDestroyContextAllocation(
    _In_ HANDLE AdapterHandle,
    _In_ HANDLE ContextAllocationHandle)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    PAGED_CODE();
    if (ContextAllocationHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(AdapterHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    Status = DxgkVidMmDestroyContextAllocation(Adapter,
                                               ContextAllocationHandle);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

/*
 * Runtime power-management notifications declared by <d3dkmddi.h>.  dxgkrnl is
 * the component that sends them, so it is where they are defined; the values
 * are those of the DEFINE_GUID declarations in that header.
 */
const GUID GUID_DXGKDDI_POWER_MANAGEMENT_PREPARE_TO_START =
    {0xcba549d4, 0xcf3a, 0x445c, {0x94, 0x68, 0x23, 0x83, 0xfd, 0x52, 0x31, 0x16}};
const GUID GUID_DXGKDDI_POWER_MANAGEMENT_STARTED =
    {0x6c929c1d, 0x7d76, 0x4538, {0x93, 0xad, 0x44, 0x9d, 0xc9, 0xfd, 0xc2, 0x39}};
const GUID GUID_DXGKDDI_POWER_MANAGEMENT_STOPPED =
    {0x0a9d9621, 0xbc21, 0x4dd4, {0xa0, 0xfc, 0xd9, 0x76, 0xe4, 0x28, 0xf7, 0x38}};

/* A miniport declaring more than this is not describing real hardware.
 * Windows rejects a count above 0xffff; this adapter-side bound is the same
 * check with the array allocation it guards kept to a sane size. */
#define DXGKP_MAX_POWER_COMPONENTS 0xffff

/* ========================================================================
 * Runtime power management (PoFx)
 *
 * Windows 11 dxgkrnl queries the miniport's component table
 * (DXGKQAITYPE_NUMPOWERCOMPONENTS, then DXGKQAITYPE_POWERCOMPONENTINFO once
 * per component), registers it with the Power Framework as a version 3
 * PO_FX_DEVICE, and afterwards never chooses an F-state itself:
 * DXGADAPTER::InitializePowerManagement does the registration and
 * DXGADAPTER::PowerRuntimeComponentIdleStateCallback_Worker is the only place
 * DxgkDdiSetPowerComponentFState is issued, in response to PoFx.
 * DxgkCbSetPowerComponentActive/Idle are how the miniport takes and drops
 * active references; they map onto PoFxActivateComponent/PoFxIdleComponent.
 *
 * DxgkDdiSetPowerComponentFState is a PASSIVE_LEVEL DDI while the PoFx
 * callbacks may arrive at DISPATCH_LEVEL, so a transition requested at raised
 * IRQL is recorded per component and issued from a work item.  Windows keeps
 * a dedicated worker thread for the same reason.
 * ====================================================================== */

static NTSTATUS
DxgkpQueryPowerComponentCount(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PULONG ComponentCount)
{
    DXGKARG_QUERYADAPTERINFO Query;
    UINT Count = 0;
    NTSTATUS Status;

    PAGED_CODE();

    *ComponentCount = 0;
    if (DXGK_CB_FULL(Adapter, DxgkDdiQueryAdapterInfo) == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(&Query, sizeof(Query));
    Query.Type = DXGKQAITYPE_NUMPOWERCOMPONENTS;
    Query.pOutputData = &Count;
    Query.OutputDataSize = sizeof(Count);
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiQueryAdapterInfo)(
                     Adapter->MiniportDeviceContext, &Query);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    if (NT_SUCCESS(Status))
        *ComponentCount = Count;
    return Status;
}

static NTSTATUS
DxgkpQueryPowerComponentInfo(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Component,
    _Out_ DXGK_POWER_RUNTIME_COMPONENT *Info)
{
    DXGKARG_QUERYADAPTERINFO Query;
    UINT ComponentIndex = (UINT)Component;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(Info, sizeof(*Info));
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(&Query, sizeof(Query));
    Query.Type = DXGKQAITYPE_POWERCOMPONENTINFO;
    Query.pInputData = &ComponentIndex;
    Query.InputDataSize = sizeof(ComponentIndex);
    Query.pOutputData = Info;
    Query.OutputDataSize = sizeof(*Info);
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiQueryAdapterInfo)(
                     Adapter->MiniportDeviceContext, &Query);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    return Status;
}

/*
 * Issue one F-state transition to the miniport.  PASSIVE_LEVEL only.
 */
static NTSTATUS
DxgkpSetPowerComponentFState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Component,
    _In_ ULONG FState)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (DXGK_CB_FULL(Adapter, DxgkDdiSetPowerComponentFState) == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DEVICE_NOT_READY;

    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiSetPowerComponentFState)(
                     Adapter->MiniportDeviceContext, (UINT)Component, (UINT)FState);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    if (NT_SUCCESS(Status))
        InterlockedExchange(&Adapter->PowerComponents[Component].LastFState, (LONG)FState);
    return Status;
}

/*
 * Complete one component's transition.  A miniport that declared
 * DriverCompletesFStateTransition answers through
 * DxgkCbCompleteFStateTransition instead, so PoFx is not completed here.
 */
static VOID
DxgkpApplyPowerComponentFState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Component,
    _In_ ULONG FState)
{
    PDXGKRNL_POWER_COMPONENT Slot = &Adapter->PowerComponents[Component];
    NTSTATUS Status;

    PAGED_CODE();

    Status = DxgkpSetPowerComponentFState(Adapter, Component, FState);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("PoFx: component %lu could not enter F%lu 0x%08lX\n",
                     Component, FState, Status);
    }

    if (Slot->Info.Flags.DriverCompletesFStateTransition)
        return;

    PoFxCompleteIdleState(Adapter->PoFxHandle, Component);
}

static BOOLEAN
DxgkpDrainPowerComponentFStates(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN Applied = FALSE;
    ULONG Component;

    PAGED_CODE();

    for (Component = 0; Component < Adapter->PowerComponentCount; ++Component)
    {
        LONG FState = InterlockedExchange(
                          &Adapter->PowerComponents[Component].RequestedFState,
                          DXGKP_POWER_FSTATE_NONE);

        if (FState == DXGKP_POWER_FSTATE_NONE)
            continue;
        DxgkpApplyPowerComponentFState(Adapter, Component, (ULONG)FState);
        Applied = TRUE;
    }
    return Applied;
}

static BOOLEAN
DxgkpPowerComponentFStatePending(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG Component;

    for (Component = 0; Component < Adapter->PowerComponentCount; ++Component)
    {
        if (InterlockedCompareExchange(&Adapter->PowerComponents[Component].RequestedFState,
                                       DXGKP_POWER_FSTATE_NONE,
                                       DXGKP_POWER_FSTATE_NONE) != DXGKP_POWER_FSTATE_NONE)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
NTAPI
DxgkpPowerFStateWorker(
    _In_ PVOID Parameter)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Parameter;

    PAGED_CODE();

    for (;;)
    {
        (VOID)DxgkpDrainPowerComponentFStates(Adapter);

        /*
         * Signal before releasing the queue slot.  A request that arrives
         * after the release resets this event itself, so a drain waiter never
         * observes a stale signalled state.
         */
        KeSetEvent(&Adapter->PowerFStateDrainedEvent, IO_NO_INCREMENT, FALSE);
        InterlockedExchange(&Adapter->PowerFStateWorkQueued, 0);
        KeMemoryBarrier();

        /* A request that lost the queue race between the drain and the
         * release above is still recorded; take it over rather than leaving
         * the component stranded in its old F-state. */
        if (!DxgkpPowerComponentFStatePending(Adapter))
            return;
        if (InterlockedCompareExchange(&Adapter->PowerFStateWorkQueued, 1, 0) != 0)
            return;
        KeClearEvent(&Adapter->PowerFStateDrainedEvent);
    }
}

static VOID
DxgkpRequestPowerComponentFState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Component,
    _In_ ULONG FState)
{
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        DxgkpApplyPowerComponentFState(Adapter, Component, FState);
        return;
    }

    InterlockedExchange(&Adapter->PowerComponents[Component].RequestedFState, (LONG)FState);
    if (InterlockedCompareExchange(&Adapter->PowerFStateWorkQueued, 1, 0) == 0)
    {
        KeClearEvent(&Adapter->PowerFStateDrainedEvent);
        ExQueueWorkItem(&Adapter->PowerFStateWorkItem, DelayedWorkQueue);
    }
}

/* --- Power Framework callbacks ---------------------------------------- */

static VOID
DxgkpPowerRuntimeComponentActiveCallback(
    _In_ PVOID Context,
    _In_ ULONG Component)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;

    if (Component >= Adapter->PowerComponentCount)
        return;
    InterlockedExchange(&Adapter->PowerComponents[Component].Active, TRUE);
}

static VOID
DxgkpPowerRuntimeComponentIdleCallback(
    _In_ PVOID Context,
    _In_ ULONG Component)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;

    if (Component >= Adapter->PowerComponentCount)
        return;
    InterlockedExchange(&Adapter->PowerComponents[Component].Active, FALSE);
    /* The idle condition is acknowledged immediately; PoFx then asks for the
     * F-state to enter.  Windows does exactly this in
     * DxgkPowerRuntimeComponentIdleCallback. */
    PoFxCompleteIdleCondition(Adapter->PoFxHandle, Component);
}

static VOID
DxgkpPowerRuntimeComponentIdleStateCallback(
    _In_ PVOID Context,
    _In_ ULONG Component,
    _In_ ULONG State)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;

    if (Component >= Adapter->PowerComponentCount)
        return;
    DxgkpRequestPowerComponentFState(Adapter, Component, State);
}

static VOID
DxgkpPowerRuntimeDevicePowerRequiredCallback(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;

    /*
     * ReactOS does not yet drive a D-state request from here.  Reporting the
     * device as powered on keeps PoFx's device-level state consistent with
     * the adapter, which is started and in D0 for the whole registration
     * lifetime; the alternative would be to leave PoFx waiting forever.
     * TODO: request D0 through the PnP power path once dxgkrnl runs adapter
     * D-state transitions (Windows: DpiRequestDevicePowerState).
     */
    PoFxReportDevicePoweredOn(Adapter->PoFxHandle);
}

static VOID
DxgkpPowerRuntimeDevicePowerNotRequiredCallback(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;

    PoFxCompleteDevicePowerNotRequired(Adapter->PoFxHandle);
}

static NTSTATUS
DxgkpPowerRuntimeControlCallback(
    _In_ PVOID Context,
    _In_ LPCGUID PowerControlCode,
    _In_opt_ PVOID InBuffer,
    _In_ SIZE_T InBufferSize,
    _Out_opt_ PVOID OutBuffer,
    _In_ SIZE_T OutBufferSize,
    _Out_opt_ PSIZE_T BytesReturned)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)Context;
    NTSTATUS Status;

    if (DXGK_CB_FULL(Adapter, DxgkDdiPowerRuntimeControlRequest) == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DEVICE_NOT_READY;

    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiPowerRuntimeControlRequest)(
                     Adapter->MiniportDeviceContext, PowerControlCode,
                     InBuffer, InBufferSize, OutBuffer, OutBufferSize,
                     BytesReturned);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    return Status;
}

/*
 * Build the PoFx registration table from the miniport's declarations and
 * register it.  Mirrors DXGADAPTER::InitializePowerManagement.
 */
static NTSTATUS
DxgkpInitializePowerManagement(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PPO_FX_DEVICE_V3 Registration = NULL;
    PDXGKRNL_POWER_COMPONENT Components = NULL;
    PPO_FX_COMPONENT_IDLE_STATE IdleStates = NULL;
    ULONG ComponentCount = 0;
    ULONG Component;
    SIZE_T RegistrationSize;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(Adapter->PoFxHandle == NULL);

    Adapter->PowerD3TransitionComponent = DXGKP_POWER_COMPONENT_NONE;
    Adapter->PowerMemoryRefreshComponent = DXGKP_POWER_COMPONENT_NONE;
    Adapter->PowerD3TransitionTwoStates = FALSE;

    Status = DxgkpQueryPowerComponentCount(Adapter, &ComponentCount);
    if (!NT_SUCCESS(Status))
    {
        /* A miniport with no runtime power components answers with a failure
         * status; BasicDisplay returns STATUS_NOT_IMPLEMENTED.  That is not
         * an adapter-start error. */
        DXGKRNL_INFO("PoFx: no runtime power components 0x%08lX\n", Status);
        return STATUS_SUCCESS;
    }
    if (ComponentCount == 0)
        return STATUS_SUCCESS;
    if (ComponentCount > DXGKP_MAX_POWER_COMPONENTS)
    {
        DXGKRNL_ERR("PoFx: miniport declared %lu power components, refusing\n",
                    ComponentCount);
        return STATUS_INVALID_PARAMETER;
    }

    Components = ExAllocatePoolZero(NonPagedPool,
                                    ComponentCount * sizeof(DXGKRNL_POWER_COMPONENT),
                                    DXGKRNL_POOL_TAG);
    RegistrationSize = FIELD_OFFSET(PO_FX_DEVICE_V3, Components) +
                       ComponentCount * sizeof(PO_FX_COMPONENT_V2);
    Registration = ExAllocatePoolZero(NonPagedPool, RegistrationSize, DXGKRNL_POOL_TAG);
    IdleStates = ExAllocatePoolZero(NonPagedPool,
                                    ComponentCount * DXGK_MAX_F_STATES *
                                        sizeof(PO_FX_COMPONENT_IDLE_STATE),
                                    DXGKRNL_POOL_TAG);
    if (Components == NULL || Registration == NULL || IdleStates == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Registration->Version = PO_FX_VERSION_V3;
    Registration->ComponentActiveConditionCallback = DxgkpPowerRuntimeComponentActiveCallback;
    Registration->ComponentIdleConditionCallback = DxgkpPowerRuntimeComponentIdleCallback;
    Registration->ComponentIdleStateCallback = DxgkpPowerRuntimeComponentIdleStateCallback;
    Registration->DevicePowerRequiredCallback = DxgkpPowerRuntimeDevicePowerRequiredCallback;
    Registration->DevicePowerNotRequiredCallback = DxgkpPowerRuntimeDevicePowerNotRequiredCallback;
    Registration->PowerControlCallback = DxgkpPowerRuntimeControlCallback;
    Registration->DeviceContext = Adapter;
    Registration->ComponentCount = ComponentCount;

    for (Component = 0; Component < ComponentCount; ++Component)
    {
        DXGK_POWER_RUNTIME_COMPONENT *Info = &Components[Component].Info;
        PPO_FX_COMPONENT_V2 Target = &Registration->Components[Component];
        PPO_FX_COMPONENT_IDLE_STATE States =
            &IdleStates[(SIZE_T)Component * DXGK_MAX_F_STATES];
        ULONG State;

        Components[Component].RequestedFState = DXGKP_POWER_FSTATE_NONE;
        Components[Component].LastFState = 0;
        Components[Component].Active = TRUE;

        Status = DxgkpQueryPowerComponentInfo(Adapter, Component, Info);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("PoFx: QueryAdapterInfo(POWERCOMPONENTINFO) component %lu failed 0x%08lX\n",
                        Component, Status);
            goto Cleanup;
        }
        if (Info->StateCount == 0 || Info->StateCount > DXGK_MAX_F_STATES)
        {
            DXGKRNL_ERR("PoFx: component %lu declared %lu F-states\n",
                        Component, Info->StateCount);
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        /* Windows rejects any component whose reserved flag bits are set. */
        if (Info->Flags.Value > 0x1f)
        {
            DXGKRNL_ERR("PoFx: component %lu reserved flags 0x%08x\n",
                        Component, Info->Flags.Value);
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Info->ComponentName[DXGK_POWER_COMPONENT_NAME_SIZE - 1] = '\0';

        /*
         * ActiveInD3 promises the component can stay usable in D3, which
         * Windows only accepts for a two-state component whose F1 entry is
         * free and which has no providers.
         */
        if (Info->Flags.ActiveInD3 &&
            (Info->StateCount != 2 ||
             Info->States[1].TransitionLatency != 0 ||
             Info->ProviderCount != 0))
        {
            DXGKRNL_ERR("PoFx: component %lu sets ActiveInD3 with states=%lu "
                        "F1-latency=%I64u providers=%lu\n",
                        Component, Info->StateCount,
                        Info->States[1].TransitionLatency, Info->ProviderCount);
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }

        switch (Info->ComponentMapping.ComponentType)
        {
            case DXGK_POWER_COMPONENT_MEMORY_REFRESH:
                if (Adapter->PowerMemoryRefreshComponent != DXGKP_POWER_COMPONENT_NONE)
                {
                    DXGKRNL_ERR("PoFx: a second MEMORY_REFRESH component %lu was declared\n",
                                Component);
                    Status = STATUS_INVALID_PARAMETER;
                    goto Cleanup;
                }
                Adapter->PowerMemoryRefreshComponent = Component;
                break;

            case DXGK_POWER_COMPONENT_D3_TRANSITION:
                if (Adapter->PowerD3TransitionComponent != DXGKP_POWER_COMPONENT_NONE)
                    break;
                if (Info->StateCount > 2)
                {
                    DXGKRNL_ERR("PoFx: D3_TRANSITION component %lu declares %lu F-states\n",
                                Component, Info->StateCount);
                    Status = STATUS_INVALID_PARAMETER;
                    goto Cleanup;
                }
                Adapter->PowerD3TransitionComponent = Component;
                Adapter->PowerD3TransitionTwoStates = (Info->StateCount == 2);
                break;

            default:
                break;
        }

        for (State = 0; State < Info->StateCount; ++State)
        {
            States[State].TransitionLatency = Info->States[State].TransitionLatency;
            States[State].ResidencyRequirement = Info->States[State].ResidencyRequirement;
            States[State].NominalPower = Info->States[State].NominalPower;
        }

        Target->Id = Info->ComponentGuid;
        Target->IdleStateCount = Info->StateCount;
        Target->IdleStates = States;
        /* The deepest state a component can be woken from is the deepest it
         * declares; ReactOS has no per-state wake capability to narrow it. */
        Target->DeepestWakeableIdleState = Info->StateCount - 1;
        if (Info->Flags.TransitionTo_F0_OnDx)
            Target->Flags |= PO_FX_COMPONENT_FLAG_F0_ON_DX;
        if (Info->Flags.NoDebounce)
            Target->Flags |= PO_FX_COMPONENT_FLAG_NO_DEBOUNCE;
    }

    Adapter->PowerComponents = Components;
    Adapter->PowerComponentCount = ComponentCount;
    KeMemoryBarrier();

    Status = PoFxRegisterDevice(Adapter->PhysicalDeviceObject,
                                (PPO_FX_DEVICE)Registration,
                                &Adapter->PoFxHandle);
    if (!NT_SUCCESS(Status))
    {
        Adapter->PowerComponents = NULL;
        Adapter->PowerComponentCount = 0;
        Adapter->PoFxHandle = NULL;
        DXGKRNL_ERR("PoFx: PoFxRegisterDevice failed 0x%08lX\n", Status);
        goto Cleanup;
    }

    ASSERT(Adapter->PoFxHandle != NULL);
    DXGKRNL_INFO("PoFx: registered %lu runtime power component(s); D3-transition=%lu (two-state=%u) memory-refresh=%lu\n",
                 ComponentCount, Adapter->PowerD3TransitionComponent,
                 Adapter->PowerD3TransitionTwoStates,
                 Adapter->PowerMemoryRefreshComponent);
    /* PoFxRegisterDevice copies the description, including the idle-state
     * arrays, so neither buffer outlives this call. */
    ExFreePoolWithTag(IdleStates, DXGKRNL_POOL_TAG);
    ExFreePoolWithTag(Registration, DXGKRNL_POOL_TAG);
    return STATUS_SUCCESS;

Cleanup:
    if (IdleStates != NULL)
        ExFreePoolWithTag(IdleStates, DXGKRNL_POOL_TAG);
    if (Registration != NULL)
        ExFreePoolWithTag(Registration, DXGKRNL_POOL_TAG);
    if (Components != NULL)
        ExFreePoolWithTag(Components, DXGKRNL_POOL_TAG);
    return Status;
}

/*
 * Hand the miniport its PoFx handle and let the framework start managing the
 * components.  Mirrors DXGADAPTER::StartRuntimePowerManagement.
 */
static VOID
DxgkpStartRuntimePowerManagement(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG Activated = 0;
    ULONG Component;
    NTSTATUS FirstFailure = STATUS_SUCCESS;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter->PoFxHandle == NULL)
        return;

    /*
     * Native dxgkrnl keeps a reference count for every power component and
     * passes only its zero-to-one and one-to-zero edges to PoFx.  The current
     * port interface can receive active callbacks before the component table
     * is registered, so those references cannot yet be replayed accurately;
     * handing the miniport a started framework in that state let it park a
     * live engine during first paint.  Keep the registered framework dormant,
     * retain one bootstrap reference per component and put each component in
     * F0 explicitly.  Miniport references remain balanced above this floor,
     * and unregistering the PoFx device discards it at adapter stop.
     */
    for (Component = 0; Component < Adapter->PowerComponentCount; ++Component)
    {
        PoFxActivateComponent(Adapter->PoFxHandle, Component, 0);
        Status = DxgkpSetPowerComponentFState(Adapter, Component, 0);
        if (NT_SUCCESS(Status))
            Activated++;
        else if (NT_SUCCESS(FirstFailure))
            FirstFailure = Status;
    }

    if (Activated == Adapter->PowerComponentCount)
    {
        DXGKRNL_INFO("PoFx: retained %lu component(s) in F0; runtime start deferred\n",
                     Activated);
    }
    else
    {
        DXGKRNL_WARN("PoFx: only %lu/%lu components reached F0; first failure "
                     "0x%08lX; runtime start deferred\n",
                     Activated, Adapter->PowerComponentCount, FirstFailure);
    }
}

static VOID
DxgkpStopRuntimePowerManagement(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    POHANDLE Handle;

    PAGED_CODE();

    Handle = Adapter->PoFxHandle;
    if (Handle == NULL)
        return;

    if (InterlockedExchange(&Adapter->PowerManagementStarted, 0) != 0 &&
        DXGK_CB_FULL(Adapter, DxgkDdiPowerRuntimeControlRequest) != NULL)
    {
        DxgkpPowerRuntimeControlCallback(Adapter,
                                         &GUID_DXGKDDI_POWER_MANAGEMENT_STOPPED,
                                         NULL, 0, NULL, 0, NULL);
    }

    /* Unregister first: no further callback can reference the component
     * table once PoFxUnregisterDevice returns. */
    Adapter->PoFxHandle = NULL;
    KeMemoryBarrier();
    PoFxUnregisterDevice(Handle);

    /* No new deferred transition can be requested now, so waiting once is
     * enough to know the worker is finished with the component table. */
    KeWaitForSingleObject(&Adapter->PowerFStateDrainedEvent,
                          Executive, KernelMode, FALSE, NULL);

    if (Adapter->PowerComponents != NULL)
    {
        ExFreePoolWithTag(Adapter->PowerComponents, DXGKRNL_POOL_TAG);
        Adapter->PowerComponents = NULL;
    }
    Adapter->PowerComponentCount = 0;
}

/*
 * The miniport takes and drops active references on its runtime power
 * components through these two slots.  Windows maps them onto
 * PoFxActivateComponent and PoFxIdleComponent (DXGADAPTER::
 * SetPowerComponentActiveCB / SetPowerComponentIdleCB); the resulting F-state
 * transitions come back through the Power Framework's idle-state callback.
 */
static VOID
APIENTRY
DxgkCbSetPowerComponentActive(
    _In_ HANDLE DeviceHandle,
    _In_ UINT   Component)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return;

    InterlockedIncrement(&Adapter->PowerActiveCalls);
    InterlockedExchange(&Adapter->PowerLastActiveComponent, (LONG)Component);
    InterlockedExchange64(&Adapter->PowerLastActiveTime100ns, (LONG64)DxgkDiagNow100ns());
    if (Adapter->PoFxHandle != NULL && Component < Adapter->PowerComponentCount)
        PoFxActivateComponent(Adapter->PoFxHandle, Component, 0);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
}

static VOID
APIENTRY
DxgkCbSetPowerComponentIdle(
    _In_ HANDLE DeviceHandle,
    _In_ UINT   Component)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return;

    InterlockedIncrement(&Adapter->PowerIdleCalls);
    InterlockedExchange(&Adapter->PowerLastIdleComponent, (LONG)Component);
    InterlockedExchange64(&Adapter->PowerLastIdleTime100ns, (LONG64)DxgkDiagNow100ns());
    if (Adapter->PoFxHandle != NULL && Component < Adapter->PowerComponentCount)
        PoFxIdleComponent(Adapter->PoFxHandle, Component, 0);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
}

/*
 * A power-control request is forwarded to the platform extension through the
 * registered PoFx device.  Without a registration there is no PEP to answer.
 */
static NTSTATUS
APIENTRY
DxgkCbPowerRuntimeControlRequest(
    _In_ HANDLE DeviceHandle,
    _In_ LPCGUID PowerControlCode,
    _In_opt_ PVOID InBuffer,
    _In_ SIZE_T InBufferSize,
    _Out_opt_ PVOID OutBuffer,
    _In_ SIZE_T OutBufferSize,
    _Out_opt_ PSIZE_T BytesReturned)
{
    PDXGKRNL_ADAPTER Adapter;

    if (BytesReturned != NULL)
        *BytesReturned = 0;
    if (PowerControlCode == NULL ||
        (InBuffer == NULL && InBufferSize != 0) ||
        (OutBuffer == NULL && OutBufferSize != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (OutBuffer != NULL && OutBufferSize != 0)
        RtlZeroMemory(OutBuffer, OutBufferSize);

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    {
        NTSTATUS Status;

        InterlockedIncrement(&Adapter->PowerControlRequestCalls);
        if (Adapter->PoFxHandle == NULL)
            Status = STATUS_NOT_SUPPORTED;
        else
        {
            Status = PoFxPowerControl(Adapter->PoFxHandle, PowerControlCode,
                                      InBuffer, InBufferSize,
                                      OutBuffer, OutBufferSize, BytesReturned);
        }
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return Status;
    }
}

/*
 * Latency and residency hints reach the platform extension through PoFx and
 * decide which idle state PopFxSelectIdleState is allowed to pick.
 */
static VOID
APIENTRY
DxgkCbSetPowerComponentLatency(
    _In_ HANDLE DeviceHandle,
    _In_ UINT ComponentIndex,
    _In_ ULONGLONG Latency)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter != NULL)
    {
        InterlockedIncrement(&Adapter->PowerLatencyCalls);
        if (Adapter->PoFxHandle != NULL && ComponentIndex < Adapter->PowerComponentCount)
            PoFxSetComponentLatency(Adapter->PoFxHandle, ComponentIndex, Latency);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    }
}

/* Matching expected-residency slot. */
static VOID
APIENTRY
DxgkCbSetPowerComponentResidency(
    _In_ HANDLE DeviceHandle,
    _In_ UINT ComponentIndex,
    _In_ ULONGLONG Residency)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter != NULL)
    {
        InterlockedIncrement(&Adapter->PowerResidencyCalls);
        if (Adapter->PoFxHandle != NULL && ComponentIndex < Adapter->PowerComponentCount)
            PoFxSetComponentResidency(Adapter->PoFxHandle, ComponentIndex, Residency);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    }
}

/*
 * A miniport that declared DriverCompletesFStateTransition finishes its
 * transition here instead of returning from DxgkDdiSetPowerComponentFState.
 * Windows completes the PoFx idle state at this point
 * (DxgkCompleteFStateTransitionCB).
 */
static VOID
APIENTRY
DxgkCbCompleteFStateTransition(
    _In_ HANDLE DeviceHandle,
    _In_ UINT ComponentIndex)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter != NULL)
    {
        InterlockedIncrement(&Adapter->PowerFStateCompleteCalls);
        if (Adapter->PoFxHandle != NULL && ComponentIndex < Adapter->PowerComponentCount)
            PoFxCompleteIdleState(Adapter->PoFxHandle, ComponentIndex);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    }
}

/* Reserved system callback; ReactOS owns no P-state transition transaction. */
static VOID
APIENTRY
DxgkCbCompletePStateTransitionSuppressed(
    _In_ HANDLE DeviceHandle,
    _In_ UINT ComponentIndex,
    _In_ UINT CompletedPState)
{
    PDXGKRNL_ADAPTER Adapter;

    UNREFERENCED_PARAMETER(ComponentIndex);
    UNREFERENCED_PARAMETER(CompletedPState);
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
}

/*
 * Context-allocation GPU VA mapping cannot exist while the matching create
 * callback is unavailable.  The public failure sentinel is address zero.
 */
static D3DGPU_VIRTUAL_ADDRESS
APIENTRY
DxgkCbMapContextAllocation(
    _In_ HANDLE DeviceHandle,
    _In_ IN_CONST_PDXGKARGCB_MAPCONTEXTALLOCATION Args)
{
    PDXGKRNL_ADAPTER Adapter;
    D3DGPU_VIRTUAL_ADDRESS Address = 0;
    NTSTATUS Status;
    LONG Count;

    PAGED_CODE();
    if (Args == NULL || Args->hAllocation == NULL ||
        Args->SizeInPages == 0 ||
        Args->OffsetInPages > MAXULONGLONG - Args->SizeInPages)
    {
        return 0;
    }
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return 0;
    Status = DxgkVidMmMapContextAllocation(Adapter,
                                           Args->hAllocation,
                                           Args->BaseAddress,
                                           Args->MinimumAddress,
                                           Args->MaximumAddress,
                                           Args->OffsetInPages,
                                           Args->SizeInPages,
                                           Args->Protection,
                                           Args->DriverProtection,
                                           &Address);
    Count = InterlockedIncrement(&Adapter->ContextAllocationMapCount);
    {
        DXGKRNL_INFO("DxgkCbMapContextAllocation #%ld seq=#%I64d: alloc=%p base=0x%I64x min=0x%I64x max=0x%I64x offset=%I64u pages=%I64u prot=0x%I64x -> status=0x%08lx va=0x%I64x\n",
                    Count, DxgkDiagSequence(), Args->hAllocation, Args->BaseAddress, Args->MinimumAddress, Args->MaximumAddress,
                    (ULONGLONG)Args->OffsetInPages, (ULONGLONG)Args->SizeInPages, Args->Protection.Value, Status, Address);
    }
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return NT_SUCCESS(Status) ? Address : 0;
}

/* DxgkCbUpdateContextAllocation: run the miniport's context-allocation update
 * through a paging operation (DXGK_OPERATION_UPDATE_CONTEXT_ALLOCATION). */
static NTSTATUS
APIENTRY
DxgkCbUpdateContextAllocation(
    _In_ HANDLE DeviceHandle,
    _In_ IN_CONST_PDXGKARGCB_UPDATECONTEXTALLOCATION Args)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;
    LONG Count;

    PAGED_CODE();
    if (Args == NULL || Args->hAllocation == NULL ||
        (Args->PrivateDriverDataSize != 0 &&
         Args->pPrivateDriverData == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    Status = DxgkVidMmUpdateContextAllocation(Adapter, Args->hAllocation, Args->pPrivateDriverData, Args->PrivateDriverDataSize);
    Count = InterlockedIncrement(&Adapter->ContextAllocationUpdateCount);
    DXGKRNL_INFO("DxgkCbUpdateContextAllocation #%ld seq=#%I64d: alloc=%p private=%u bytes -> 0x%08lx\n", Count, DxgkDiagSequence(), Args->hAllocation, Args->PrivateDriverDataSize, Status);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

static NTSTATUS
APIENTRY
DxgkCbReserveGpuVirtualAddressRange(
    _In_ HANDLE DeviceHandle,
    _Inout_ INOUT_PDXGKARGCB_RESERVEGPUVIRTUALADDRESSRANGE Args)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_PROCESS Process = NULL;
    NTSTATUS ProcessStatus;
    NTSTATUS Status;

    PAGED_CODE();
    if (Args == NULL)
        return STATUS_INVALID_PARAMETER;
    Args->StartVirtualAddress = 0;

    if (Args->hDxgkProcess == NULL || Args->SizeInBytes == 0 ||
        Args->Alignment == 0 ||
        (Args->Alignment & (Args->Alignment - 1)) != 0 ||
        (Args->Flags & ~1U) != 0 ||
        (Args->BaseAddress & (Args->Alignment - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    ProcessStatus = DxgkValidateCreatingProcessHandle(Adapter,
                                                       Args->hDxgkProcess,
                                                       &Process);
    Status = ProcessStatus;
    if (NT_SUCCESS(ProcessStatus))
    {
        Status = DxgkGpuVaReserveDriverRange(
                     Adapter,
                     Process,
                     Args->BaseAddress,
                     Args->SizeInBytes,
                     Args->Alignment,
                     Args->AllowUserModeMapping != 0,
                     &Args->StartVirtualAddress);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DxgkCbReserveGpuVirtualAddressRange failed: process=%p base=0x%I64x size=0x%I64x status=0x%08lx\n",
                Args->hDxgkProcess,
                Args->BaseAddress,
                Args->SizeInBytes,
                Status);
    }
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

/* Protected-session teardown is inert while protected sessions stay off. */
static VOID
APIENTRY
DxgkCbHardwareContentProtectionTeardownSuppressed(
    _In_ HANDLE DeviceHandle,
    _In_ UINT Flags)
{
    PDXGKRNL_ADAPTER Adapter;

    UNREFERENCED_PARAMETER(Flags);
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
}

/* Public MPO support is off, so there is no DWM fallback state to notify. */
static VOID
APIENTRY
DxgkCbMultiPlaneOverlayDisabledSuppressed(
    _In_ HANDLE DeviceHandle,
    _In_ UINT VidPnSourceId)
{
    PDXGKRNL_ADAPTER Adapter;

    UNREFERENCED_PARAMETER(VidPnSourceId);
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
}

/* No SR-IOV virtual-function mitigated-range owner is advertised. */
static VOID
APIENTRY
DxgkCbMitigatedRangeUpdateSuppressed(
    _In_ HANDLE DeviceHandle,
    _In_ ULONG VirtualFunctionIndex)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(VirtualFunctionIndex);
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
}

/*
 * Hardware-context invalidation is a scheduler-owned operation.  ReactOS
 * does not advertise hardware queues or own their invalidation/cleanup
 * transaction, so validate the public envelope and fail without changing
 * an ordinary context.
 */
static NTSTATUS
APIENTRY
DxgkCbInvalidateHwContextNotSupported(
    IN_CONST_PDXGKARGCB_INVALIDATEHWCONTEXT Args)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (Args == NULL || Args->hAdapter == NULL || Args->hHwContext == NULL ||
        (Args->Flags.Value & ~1U) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(Args->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/*
 * The WDDM 2.2 connector callback announces entries in the miniport's
 * ordered QueryConnectionChange queue.  The legacy QueryChildStatus route
 * is not equivalent, so do not report that the queue was accepted until a
 * PASSIVE_LEVEL drain worker and topology transaction exist.
 */
static NTSTATUS
APIENTRY
DxgkCbIndicateConnectorChangeNotSupported(
    IN_CONST_HANDLE DeviceHandle)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    DXGKRNL_WARN("CONNECTOR_CHANGE: adapter %p requested a connection-queue "
                 "drain; no provider is available\n", Adapter);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/*
 * UEFI framebuffer ranges can be unblocked only by the owner that first
 * blocked the exact segment ranges.  ReactOS records no such ownership, so
 * accept no transition and leave the caller's range array untouched.
 */
static NTSTATUS
APIENTRY
DxgkCbUnblockUEFIFrameBufferRangesNotSupported(
    IN_CONST_HANDLE DeviceHandle,
    IN_CONST_PDXGK_SEGMENTMEMORYSTATE SegmentMemoryState)
{
    PDXGKRNL_ADAPTER Adapter;

    if (SegmentMemoryState == NULL ||
        (SegmentMemoryState->NumUEFIFrameBufferRanges != 0 &&
         SegmentMemoryState->pMemoryRanges == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/* Protected-session objects and their status state machine remain off. */
static NTSTATUS
APIENTRY
DxgkCbSetProtectedSessionStatusNotSupported(
    IN_CONST_PDXGKARGCB_PROTECTEDSESSIONSTATUS ProtectedSessionStatus)
{
    PAGED_CODE();
    if (ProtectedSessionStatus == NULL ||
        ProtectedSessionStatus->hProtectedSession == NULL ||
        ProtectedSessionStatus->Status >
            DXGK_PROTECTED_SESSION_STATUS_INVALID)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_NOT_SUPPORTED;
}

/* No framebuffer-save section or commit accounting is advertised. */
static NTSTATUS
APIENTRY
DxgkCbPinFrameBufferForSaveNotSupported(
    IN_CONST_HANDLE DeviceHandle,
    INOUT_PDXGKARGCB_PINFRAMEBUFFERFORSAVE PinFrameBufferForSave)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (PinFrameBufferForSave == NULL)
        return STATUS_INVALID_PARAMETER;
    PinFrameBufferForSave->pMdl = NULL;
    if (PinFrameBufferForSave->PhysicalAdapterIndex != 0 ||
        PinFrameBufferForSave->CommitSize == 0 ||
        (PinFrameBufferForSave->CommitSize & (PAGE_SIZE - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/* The matching unpin cannot succeed while pin never publishes an MDL. */
static NTSTATUS
APIENTRY
DxgkCbUnpinFrameBufferForSaveNotSupported(
    IN_CONST_HANDLE DeviceHandle,
    IN_CONST_PDXGKARGCB_UNPINFRAMEBUFFERFORSAVE UnpinFrameBufferForSave)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (UnpinFrameBufferForSave == NULL ||
        UnpinFrameBufferForSave->PhysicalAdapterIndex != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/* No per-physical-adapter framebuffer section exists to map. */
static NTSTATUS
APIENTRY
DxgkCbMapFrameBufferPointerNotSupported(
    IN_CONST_HANDLE DeviceHandle,
    INOUT_PDXGKARGCB_MAPFRAMEBUFFERPOINTER MapFrameBufferPointer)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (MapFrameBufferPointer == NULL)
        return STATUS_INVALID_PARAMETER;
    MapFrameBufferPointer->pBaseAddress = NULL;
    if (MapFrameBufferPointer->PhysicalAdapterIndex != 0 ||
        MapFrameBufferPointer->Size == 0 ||
        MapFrameBufferPointer->Offset >
            MAXULONG_PTR - MapFrameBufferPointer->Size)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/* The matching unmap rejects pointers that this port never published. */
static NTSTATUS
APIENTRY
DxgkCbUnmapFrameBufferPointerNotSupported(
    IN_CONST_HANDLE DeviceHandle,
    IN_CONST_PDXGKARGCB_UNMAPFRAMEBUFFERPOINTER UnmapFrameBufferPointer)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (UnmapFrameBufferPointer == NULL ||
        UnmapFrameBufferPointer->PhysicalAdapterIndex != 0 ||
        UnmapFrameBufferPointer->pBaseAddress == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/* Graphics IOMMU domains remain unadvertised; never return an identity map. */
static NTSTATUS
APIENTRY
DxgkCbMapMdlToIoMmuNotSupported(
    IN_CONST_HANDLE DeviceHandle,
    INOUT_PDXGKARGCB_MAPMDLTOIOMMU MapMdlToIoMmu)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (MapMdlToIoMmu == NULL)
        return STATUS_INVALID_PARAMETER;
    MapMdlToIoMmu->hMemoryHandle = NULL;
    if (MapMdlToIoMmu->pMdl == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/* No valid IOMMU handle can exist while the matching map always fails. */
static VOID
APIENTRY
DxgkCbUnmapMdlFromIoMmuSuppressed(
    IN_CONST_HANDLE DeviceHandle,
    IN_CONST_PDXGKARGCB_UNMAPMDLFROMIOMMU UnmapMdlFromIoMmu)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(UnmapMdlFromIoMmu);
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
}

/*
 * ReactOS has no bounded diagnostic ingestion sink.  Validate the public
 * WDDM envelope at the callback's DISPATCH_LEVEL ceiling, but never claim
 * that an event was recorded.
 */
static NTSTATUS
APIENTRY
DxgkCbReportDiagnosticNotSupported(
    _In_ HANDLE DeviceHandle,
    IN_PDXGK_DIAGNOSTIC_HEADER Diagnostic)
{
    PDXGKRNL_ADAPTER Adapter;
    ULONG RequiredSize;

    if (Diagnostic == NULL || Diagnostic->Reserved != 0)
        return STATUS_INVALID_PARAMETER;
    if (Diagnostic->Size < sizeof(*Diagnostic))
        return STATUS_BUFFER_TOO_SMALL;

    if (Diagnostic->Category.Value == DXGK_DIAGCAT_NOTIFICATIONS_MASK)
    {
        if (Diagnostic->Type.Value != DXGK_DIAG_NOTIFICATIONS_PSR_SW_MASK &&
            Diagnostic->Type.Value != DXGK_DIAG_NOTIFICATIONS_PSR_HW_MASK)
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize = sizeof(DXGK_DIAGNOSTIC_PSR);
    }
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    else if (Diagnostic->Category.Value == DXGK_DIAGCAT_PROGRESSIONS_MASK)
    {
        if (Diagnostic->Type.Value !=
            DXGK_DIAG_PROGRESSIONS_SYNCLOCK_ENABLE_SYNC_MASK)
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize = sizeof(DXGK_DIAGNOSTIC_SYNCLOCK_ENABLESYNC);
    }
#endif
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Diagnostic->Size < RequiredSize)
        return STATUS_BUFFER_TOO_SMALL;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/*
 * Driver hot update is not an advertised lifecycle.  On failure ownership of
 * every input buffer and MDL remains with the miniport.
 */
static NTSTATUS
APIENTRY
DxgkCbSaveMemoryForHotUpdateNotSupported(
    IN_CONST_HANDLE DeviceHandle,
    IN_CONST_PDXGKARGCB_SAVEMEMORYFORHOTUPDATE Args)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (Args == NULL ||
        (Args->NumDataMemoryRanges != 0 &&
         Args->pDataMemoryRanges == NULL) ||
        (Args->DataSize != 0 && Args->pData == NULL &&
         Args->pDataMdl == NULL) ||
        (Args->MetaDataSize != 0 && Args->pMetaData == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/* No cursor capability-change transaction is owned by the present path. */
static NTSTATUS
APIENTRY
DxgkCbNotifyCursorSupportChangeNotSupported(
    IN_CONST_PDXGKARGCB_NOTIFYCURSORSUPPORTCHANGE Args)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (Args == NULL || Args->DeviceHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(Args->DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    if (Args->VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_PARAMETER;
    }

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/* No framebuffer-save section exists from which a WDDM 2.9 ADL can be made. */
static NTSTATUS
APIENTRY
DxgkCbPinFrameBufferForSave2NotSupported(
    IN_CONST_HANDLE DeviceHandle,
    INOUT_PDXGKARGCB_PINFRAMEBUFFERFORSAVE2 Args)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (Args == NULL)
        return STATUS_INVALID_PARAMETER;
    Args->pAdl = NULL;
    if (Args->PhysicalAdapterIndex != 0 || Args->CommitSize == 0 ||
        (Args->CommitSize & (PAGE_SIZE - 1)) != 0 ||
        (Args->Flags.Value & ~1U) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_NOT_SUPPORTED;
}

/*
 * Doorbells stay outside the clean-room feature gate.  No handles are
 * published, no mapping is rotated, and no user-visible status is changed.
 */
static NTSTATUS
APIENTRY
DxgkCbDisconnectDoorbellNotSupported(
    INOUT_PDXGKARGCB_DISCONNECTDOORBELL Args)
{
    PAGED_CODE();
    if (Args == NULL || Args->hHwQueue == NULL || Args->hDoorbell == NULL ||
        Args->Flags.Value != 0 ||
        (Args->DisconnectReason !=
             D3DDDI_DOORBELLSTATUS_DISCONNECTED_RETRY &&
         Args->DisconnectReason !=
             D3DDDI_DOORBELLSTATUS_DISCONNECTED_ABORT))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}

#define DXGKP_MAX_ACPI_METHOD_ARGUMENTS 7

static BOOLEAN
DxgkpValidateAcpiComplexInput(
    _In_reads_bytes_(InputSize)
        PACPI_EVAL_INPUT_BUFFER_COMPLEX InputBuffer,
    _In_ ULONG InputSize)
{
    PACPI_METHOD_ARGUMENT Argument;
    ULONG ArgumentLength;
    ULONG ArgumentOffset;
    ULONG Index;

    if (InputBuffer == NULL ||
        InputSize < sizeof(*InputBuffer) ||
        InputBuffer->ArgumentCount > DXGKP_MAX_ACPI_METHOD_ARGUMENTS)
    {
        return FALSE;
    }

    ArgumentOffset = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Argument = InputBuffer->Argument;
    for (Index = 0; Index < InputBuffer->ArgumentCount; ++Index)
    {
        if (ArgumentOffset > InputSize ||
            sizeof(*Argument) > InputSize - ArgumentOffset)
        {
            return FALSE;
        }

        ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH(Argument->DataLength);
        if (ArgumentLength > InputSize - ArgumentOffset)
            return FALSE;

        ArgumentOffset += ArgumentLength;
        Argument = (PACPI_METHOD_ARGUMENT)
            ((PUCHAR)InputBuffer + ArgumentOffset);
    }

    return TRUE;
}

/*
 * DxgkCbEvalAcpiMethod — offset 0x10
 *
 * Evaluate a method in the display adapter's ACPI namespace.  The PCI PDO
 * forwards IOCTL_ACPI_EVAL_METHOD to the matching ACPI namespace device.
 * DXGK_ACPI_USE_ACPI_UID names a child by its ACPI _ADR directly instead of
 * by the ChildUid the miniport enumerated.
 */
static VOID
DxgkpReportAcpiEval(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG DeviceUid,
    _In_ ULONG Signature,
    _In_reads_bytes_opt_(InputSize)
        PACPI_EVAL_INPUT_BUFFER_COMPLEX InputBuffer,
    _In_ ULONG InputSize,
    _In_reads_bytes_opt_(OutputSize)
        PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputSize,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    CHAR MethodName[5] = "????";
    ULONG ArgumentCount = 0;
    ULONG OutputCount = 0;
    ULONG OutputLength = 0;
    LONG Count;

    /* A miniport that rejects start rarely says which firmware answer it
     * disliked, so the first evaluations of every adapter stay visible. */
    Count = InterlockedIncrement(&Adapter->AcpiEvalCount);
    if (Count > 32 && NT_SUCCESS(Status))
        return;

    if (InputBuffer != NULL &&
        InputSize >= FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument))
    {
        RtlCopyMemory(MethodName,
                      InputBuffer->MethodName,
                      sizeof(InputBuffer->MethodName));
        ArgumentCount = InputBuffer->ArgumentCount;
    }
    if (OutputBuffer != NULL &&
        Information >= FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument))
    {
        OutputCount = OutputBuffer->Count;
        OutputLength = OutputBuffer->Length;
    }

    DXGKRNL_INFO("DxgkCbEvalAcpiMethod #%ld: adapter=%p uid=0x%lx sig=%.4s "
                "method=%s args=%lu in=%lu out=%lu -> status=0x%08lx "
                "info=%Iu count=%lu length=%lu\n",
                Count, Adapter, DeviceUid, (PCSTR)&Signature, MethodName,
                ArgumentCount, InputSize, OutputSize, Status, Information,
                OutputCount, OutputLength);
}

static NTSTATUS
APIENTRY
DxgkCbEvalAcpiMethod(
    _In_  HANDLE DeviceHandle,
    _In_  ULONG  DeviceUid,
    _In_reads_bytes_(AcpiInputSize)
          PACPI_EVAL_INPUT_BUFFER_COMPLEX AcpiInputBuffer,
    _In_  ULONG  AcpiInputSize,
    _Out_writes_bytes_(AcpiOutputSize)
          PACPI_EVAL_OUTPUT_BUFFER AcpiOutputBuffer,
    _In_  ULONG  AcpiOutputSize)
{
    PDXGKRNL_ADAPTER Adapter;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    ULONG Signature = 0;
    ULONG ChildAcpiUid;
    ULONG IoControlCode;
    PVOID IoInputBuffer;
    ULONG IoInputSize;
    PACPI_PCI_CHILD_EVAL_INPUT_BUFFER ChildInput = NULL;

    PAGED_CODE();

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    RtlZeroMemory(&IoStatus, sizeof(IoStatus));
    Status = STATUS_INVALID_PARAMETER;
    if (AcpiInputBuffer != NULL && AcpiInputSize >= sizeof(Signature))
        Signature = AcpiInputBuffer->Signature;

    if (!DxgkpValidateAcpiComplexInput(AcpiInputBuffer, AcpiInputSize) ||
        (AcpiOutputBuffer == NULL && AcpiOutputSize != 0) ||
        (AcpiOutputBuffer != NULL &&
         AcpiOutputSize < sizeof(ACPI_EVAL_OUTPUT_BUFFER)))
    {
        goto Done;
    }

    if (Signature != ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE &&
        Signature != DXGK_ACPI_PASS_ARGS_TO_CHILDREN &&
        Signature != DXGK_ACPI_USE_ACPI_UID)
    {
        goto Done;
    }

    /* The public contract restores the caller's marker before returning. */
    AcpiInputBuffer->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;

    if (Adapter->PhysicalDeviceObject == NULL)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Done;
    }

    IoControlCode = IOCTL_ACPI_EVAL_METHOD;
    IoInputBuffer = AcpiInputBuffer;
    IoInputSize = AcpiInputSize;

    if (DeviceUid != DISPLAY_ADAPTER_HW_ID)
    {
        if (Signature == DXGK_ACPI_USE_ACPI_UID)
        {
            ChildAcpiUid = DeviceUid;
        }
        else
        {
            Status = DxgkPnpResolveChildAcpiUid(Adapter,
                                                DeviceUid,
                                               &ChildAcpiUid);
            if (!NT_SUCCESS(Status))
                goto Done;
        }
        if (AcpiInputSize > MAXULONG - sizeof(*ChildInput))
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Done;
        }

        IoInputSize = sizeof(*ChildInput) + AcpiInputSize;
        ChildInput = ExAllocatePoolWithTag(PagedPool,
                                           IoInputSize,
                                           TAG_DXGK_RESOURCES);
        if (ChildInput == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }

        RtlZeroMemory(ChildInput, sizeof(*ChildInput));
        ChildInput->Signature =
            ACPI_PCI_CHILD_EVAL_INPUT_BUFFER_SIGNATURE;
        ChildInput->ChildAcpiUid = ChildAcpiUid;
        ChildInput->TotalSize = IoInputSize;
        ChildInput->InputBufferOffset = sizeof(*ChildInput);
        ChildInput->InputBufferSize = AcpiInputSize;
        RtlCopyMemory(
            ACPI_PCI_CHILD_EVAL_GET_INPUT_BUFFER(ChildInput),
            AcpiInputBuffer,
            AcpiInputSize);
        IoControlCode = IOCTL_ACPI_EVAL_METHOD_FOR_PCI_CHILD;
        IoInputBuffer = ChildInput;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(
              IoControlCode,
              Adapter->PhysicalDeviceObject,
              IoInputBuffer,
              IoInputSize,
              AcpiOutputBuffer,
              AcpiOutputSize,
              FALSE,
              &Event,
              &IoStatus);
    if (Irp == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    Status = IoCallDriver(Adapter->PhysicalDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatus.Status;
    }

Done:
    DxgkpReportAcpiEval(Adapter,
                        DeviceUid,
                        Signature,
                        AcpiInputBuffer,
                        AcpiInputSize,
                        AcpiOutputBuffer,
                        AcpiOutputSize,
                        Status,
                        IoStatus.Information);
    if (ChildInput != NULL)
        ExFreePoolWithTag(ChildInput, TAG_DXGK_RESOURCES);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
static BOOLEAN
DxgkpKmdCpuEventFeatureAvailable(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
#if (REACTOS_WDDM_TARGET_LEVEL >= 3000) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
    if (Adapter == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->UseDodLayout ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_3_0))
    {
        return FALSE;
    }

    return DXGK_CB_FULL(Adapter, DxgkDdiCreateCpuEvent) != NULL &&
           DXGK_CB_FULL(Adapter, DxgkDdiDestroyCpuEvent) != NULL;
#else
    UNREFERENCED_PARAMETER(Adapter);
    return FALSE;
#endif
}

static NTSTATUS
APIENTRY
DxgkCbIsFeatureEnabled(
    INOUT_PDXGKARGCB_ISFEATUREENABLED Args)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (Args == NULL)
        return STATUS_INVALID_PARAMETER;
    Args->Enabled = FALSE;

    Adapter = DxgkpHandleToAdapter(Args->DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (Args->FeatureId == DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT)
        Args->Enabled = DxgkpKmdCpuEventFeatureAvailable(Adapter);

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
static NTSTATUS
APIENTRY
DxgkCbQueryFeatureSupport(
    INOUT_PDXGKARGCB_QUERYFEATURESUPPORT Args)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();
    if (Args == NULL ||
        Args->DriverSupportState > DXGK_FEATURE_SUPPORT_ALWAYS_ON)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Args->Enabled = FALSE;

    Adapter = DxgkpHandleToAdapter(Args->DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (Args->FeatureId == DXGK_FEATURE_KMD_SIGNAL_CPU_EVENT &&
        Args->DriverSupportState >= DXGK_FEATURE_SUPPORT_STABLE)
    {
        Args->Enabled = DxgkpKmdCpuEventFeatureAvailable(Adapter);
    }

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}
#endif

/*
 * Native dxgkrnl does not advertise sizeof(DXGKRNL_INTERFACE) blindly.  Each
 * accepted WDDM 2.x/3.x selector receives the public prefix ending at that
 * revision's last callback.  Starting with WDDM 2.8, native publishes its
 * whole current callback buffer and leaves unsupported callbacks NULL.
 *
 * Keep the sizes tied to WDK field ends rather than pointer-size literals.
 * These assertions also prove the x86 sizes independently of the native
 * amd64/arm64 branch constants.
 */
#ifdef _WIN64
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbExcludeAdapterAccess) == 0xB8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompleteFStateTransition) == 0x100);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompletePStateTransition) == 0x108);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbHardwareContentProtectionTeardown) == 0x138);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbMitigatedRangeUpdate) == 0x148);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership2) == 0x168);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSetProtectedSessionStatus) == 0x170);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbReportDiagnostic) == 0x1C8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSignalEvent) == 0x1D0);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSaveMemoryForHotUpdate) == 0x1E0);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbDisconnectDoorbell) == 0x240);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x240);
#else
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbExcludeAdapterAccess) == 0x60);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompleteFStateTransition) == 0x84);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbCompletePStateTransition) == 0x88);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbHardwareContentProtectionTeardown) == 0xA0);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbMitigatedRangeUpdate) == 0xA8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership2) == 0xB8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSetProtectedSessionStatus) == 0xBC);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbReportDiagnostic) == 0xE8);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSignalEvent) == 0xEC);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSaveMemoryForHotUpdate) == 0xF4);
C_ASSERT(DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbDisconnectDoorbell) == 0x124);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x124);
#endif

static VOID
DxgkpSelectInterfaceAdvertisement(
    _In_ ULONG RequestedVersion,
    _Out_ PULONG AdvertisedSize,
    _Out_ PULONG AdvertisedVersion)
{
    if (RequestedVersion < DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    {
        *AdvertisedSize = 0;
        *AdvertisedVersion = 0;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    {
        *AdvertisedSize = DXGKP_FIELD_END(
            DXGKRNL_INTERFACE,
            DxgkCbHardwareContentProtectionTeardown);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_0;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbMitigatedRangeUpdate);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_1;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    {
        *AdvertisedSize = DXGKP_FIELD_END(
            DXGKRNL_INTERFACE,
            DxgkCbAcquirePostDisplayOwnership2);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_2;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    {
        *AdvertisedSize = DXGKP_FIELD_END(
            DXGKRNL_INTERFACE,
            DxgkCbSetProtectedSessionStatus);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_3;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbReportDiagnostic);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_4;
    }
    else if (RequestedVersion <= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSignalEvent);
        *AdvertisedVersion = DXGKDDI_INTERFACE_VERSION_WDDM2_5;
    }
    else if (RequestedVersion < DXGKDDI_INTERFACE_VERSION_WDDM2_8)
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbSaveMemoryForHotUpdate);
        *AdvertisedVersion = RequestedVersion;
    }
    else
    {
        *AdvertisedSize =
            DXGKP_FIELD_END(DXGKRNL_INTERFACE, DxgkCbDisconnectDoorbell);
        *AdvertisedVersion = RequestedVersion;
    }
}

/*
 * DxgkpFillInterface
 *
 * Populate a DXGK_INTERFACE (DXGKRNL_INTERFACE) structure with the callbacks
 * dxgkrnl provides to the miniport at DxgkDdiStartDevice time.
 *
 * Field order must match the official Windows WDK DXGKRNL_INTERFACE layout.
 * See dispmprt.h for the verified field-by-field offset table.
 *
 * IRQL: PASSIVE_LEVEL
 */
static VOID
DxgkpFillInterface(
    _In_  PDXGKRNL_ADAPTER Adapter,
    _Out_ PDXGK_INTERFACE  Interface)
{
    ULONG RequestedVersion;

    RequestedVersion = Adapter->MiniportContext->InitData.s.Version;
    RtlZeroMemory(Interface, sizeof(*Interface));

    DxgkpSelectInterfaceAdvertisement(
        RequestedVersion,
        &Interface->Size,
        &Interface->Version);
    Interface->DeviceHandle = (HANDLE)Adapter;

    DXGKRNL_INFO("DxgkpFillInterface: DeviceHandle=%p Size=%u "
                "RequestedVersion=0x%lX AdvertisedVersion=0x%lX\n",
                  Interface->DeviceHandle,
                  Interface->Size,
                  RequestedVersion,
                  Interface->Version);

    /* The WDDM 2.x baseline retains the original callback prefix. */
    Interface->DxgkCbEvalAcpiMethod                = DxgkCbEvalAcpiMethod;     /* 0x10 */
    Interface->DxgkCbGetDeviceInformation          = DxgkCbGetDeviceInformation;  /* 0x18 */
    Interface->DxgkCbIndicateChildStatus           = DxgkCbIndicateChildStatus;   /* 0x20 */
    Interface->DxgkCbMapMemory                     = DxgkCbMapMemory;             /* 0x28 */
    Interface->DxgkCbQueueDpc                      = DxgkCbQueueDpc;             /* 0x30 */
    Interface->DxgkCbQueryServices                 = DxgkCbQueryServices;         /* 0x38 */
    Interface->DxgkCbReadDeviceSpace               = DxgkCbReadDeviceSpace;       /* 0x40 */
    Interface->DxgkCbSynchronizeExecution          = DxgkCbSynchronizeExecution;  /* 0x48 */
    Interface->DxgkCbUnmapMemory                   = DxgkCbUnmapMemory;              /* 0x50 */
    Interface->DxgkCbWriteDeviceSpace              = DxgkCbWriteDeviceSpace;      /* 0x58 */
    Interface->DxgkCbIsDevicePresent               = DxgkCbIsDevicePresent;   /* 0x60 */
    Interface->DxgkCbGetHandleData                 = DxgkCbGetHandleData;          /* 0x68 */
    Interface->DxgkCbGetHandleParent               = DxgkCbGetHandleParent;   /* 0x70 */
    Interface->DxgkCbEnumHandleChildren            = DxgkCbEnumHandleChildren; /* 0x78 */
    Interface->DxgkCbNotifyInterrupt               = DxgkCbNotifyInterrupt;       /* 0x80 */
    Interface->DxgkCbNotifyDpc                     = DxgkCbNotifyDpc;             /* 0x88 */
    Interface->DxgkCbQueryVidPnInterface           = DxgkCbQueryVidPnInterface; /* 0x90 */
    Interface->DxgkCbQueryMonitorInterface         = DxgkCbQueryMonitorInterface; /* 0x98 */
    Interface->DxgkCbGetCaptureAddress             = DxgkCbGetCaptureAddress; /* 0xa0 */

    Interface->DxgkCbLogEtwEvent = DxgkCbLogEtwEventDisabled; /* 0xa8 */
    Interface->DxgkCbExcludeAdapterAccess = DxgkCbExcludeAdapterAccessNotSupported; /* 0xb0 */
    Interface->DxgkCbCreateContextAllocation = DxgkCbCreateContextAllocation; /* 0xb8 */
    Interface->DxgkCbDestroyContextAllocation = DxgkCbDestroyContextAllocation; /* 0xc0 */
    Interface->DxgkCbSetPowerComponentActive = DxgkCbSetPowerComponentActive; /* 0xc8 */
    Interface->DxgkCbSetPowerComponentIdle = DxgkCbSetPowerComponentIdle; /* 0xd0 */
    Interface->DxgkCbAcquirePostDisplayOwnership = DxgkCbAcquirePostDisplayOwnership; /* 0xd8 */
    Interface->DxgkCbPowerRuntimeControlRequest = DxgkCbPowerRuntimeControlRequest; /* 0xe0 */
    Interface->DxgkCbSetPowerComponentLatency = DxgkCbSetPowerComponentLatency; /* 0xe8 */
    Interface->DxgkCbSetPowerComponentResidency = DxgkCbSetPowerComponentResidency; /* 0xf0 */
    Interface->DxgkCbCompleteFStateTransition = DxgkCbCompleteFStateTransition; /* 0xf8 */
    Interface->DxgkCbCompletePStateTransition = DxgkCbCompletePStateTransitionSuppressed; /* 0x100 */

    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
    {
        Interface->DxgkCbMapContextAllocation = DxgkCbMapContextAllocation; /* 0x108 */
        Interface->DxgkCbUpdateContextAllocation = DxgkCbUpdateContextAllocation; /* 0x110 */
        Interface->DxgkCbReserveGpuVirtualAddressRange = DxgkCbReserveGpuVirtualAddressRange; /* 0x118 */
        Interface->DxgkCbAcquireHandleData = DxgkCbAcquireHandleData; /* 0x120 */
        Interface->DxgkCbReleaseHandleData = DxgkCbReleaseHandleData; /* 0x128 */
        Interface->DxgkCbHardwareContentProtectionTeardown = DxgkCbHardwareContentProtectionTeardownSuppressed; /* 0x130 */
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_1))
    {
        Interface->DxgkCbMultiPlaneOverlayDisabled = DxgkCbMultiPlaneOverlayDisabledSuppressed; /* 0x138 */
        Interface->DxgkCbMitigatedRangeUpdate = DxgkCbMitigatedRangeUpdateSuppressed; /* 0x140 */
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_2))
    {
        Interface->DxgkCbInvalidateHwContext =
            DxgkCbInvalidateHwContextNotSupported; /* 0x148 */
        Interface->DxgkCbIndicateConnectorChange =
            DxgkCbIndicateConnectorChangeNotSupported; /* 0x150 */
        Interface->DxgkCbUnblockUEFIFrameBufferRanges =
            DxgkCbUnblockUEFIFrameBufferRangesNotSupported; /* 0x158 */
        Interface->DxgkCbAcquirePostDisplayOwnership2 =
            DxgkCbAcquirePostDisplayOwnership2; /* 0x160 */
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_3))
    {
        Interface->DxgkCbSetProtectedSessionStatus =
            DxgkCbSetProtectedSessionStatusNotSupported; /* 0x168 */
    }
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_4))
    {
        Interface->DxgkCbAllocateContiguousMemory =
            DxgkCbAllocateContiguousMemory; /* 0x170 */
        Interface->DxgkCbFreeContiguousMemory =
            DxgkCbFreeContiguousMemory; /* 0x178 */
        Interface->DxgkCbAllocatePagesForMdl =
            DxgkCbAllocatePagesForMdl; /* 0x180 */
        Interface->DxgkCbFreePagesFromMdl =
            DxgkCbFreePagesFromMdl; /* 0x188 */
        Interface->DxgkCbPinFrameBufferForSave =
            DxgkCbPinFrameBufferForSaveNotSupported; /* 0x190 */
        Interface->DxgkCbUnpinFrameBufferForSave =
            DxgkCbUnpinFrameBufferForSaveNotSupported; /* 0x198 */
        Interface->DxgkCbMapFrameBufferPointer =
            DxgkCbMapFrameBufferPointerNotSupported; /* 0x1a0 */
        Interface->DxgkCbUnmapFrameBufferPointer =
            DxgkCbUnmapFrameBufferPointerNotSupported; /* 0x1a8 */
        Interface->DxgkCbMapMdlToIoMmu =
            DxgkCbMapMdlToIoMmuNotSupported; /* 0x1b0 */
        Interface->DxgkCbUnmapMdlFromIoMmu =
            DxgkCbUnmapMdlFromIoMmuSuppressed; /* 0x1b8 */
        Interface->DxgkCbReportDiagnostic =
            DxgkCbReportDiagnosticNotSupported; /* 0x1c0 */
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 3000) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_5))
    {
        Interface->DxgkCbSignalEvent = DxgkCbSignalEvent;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2600) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_6))
    {
        Interface->DxgkCbIsFeatureEnabled = DxgkCbIsFeatureEnabled;
        Interface->DxgkCbSaveMemoryForHotUpdate =
            DxgkCbSaveMemoryForHotUpdateNotSupported;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2800) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_8))
    {
        Interface->DxgkCbNotifyCursorSupportChange =
            DxgkCbNotifyCursorSupportChangeNotSupported;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2900) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_9))
    {
        Interface->DxgkCbQueryFeatureSupport = DxgkCbQueryFeatureSupport;
        Interface->DxgkCbCreatePhysicalMemoryObject =
            DxgkCbCreatePhysicalMemoryObject;
        Interface->DxgkCbDestroyPhysicalMemoryObject =
            DxgkCbDestroyPhysicalMemoryObject;
        Interface->DxgkCbMapPhysicalMemory = DxgkCbMapPhysicalMemory;
        Interface->DxgkCbUnmapPhysicalMemory = DxgkCbUnmapPhysicalMemory;
        Interface->DxgkCbAllocateAdl = DxgkCbAllocateAdl;
        Interface->DxgkCbFreeAdl = DxgkCbFreeAdl;
        Interface->DxgkCbOpenPhysicalMemoryObject =
            DxgkCbOpenPhysicalMemoryObject;
        Interface->DxgkCbClosePhysicalMemoryObject =
            DxgkCbClosePhysicalMemoryObject;
        Interface->DxgkCbPinFrameBufferForSave2 =
            DxgkCbPinFrameBufferForSave2NotSupported;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 3100) && \
    (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Interface->Version, DXGK_CAPS_CORE_LEVEL_WDDM_3_1))
    {
        Interface->DxgkCbDisconnectDoorbell =
            DxgkCbDisconnectDoorbellNotSupported;
    }
#endif

}

/*
 * DxgkpHandleToAdapter
 *
 * Validate and dereference a DeviceHandle as a DXGKRNL_ADAPTER pointer.
 * All DxgkCb* callbacks call this helper to convert the opaque handle
 * supplied by the miniport back to the adapter object.
 *
 * Returns the adapter pointer with ReverseCallbackRundownRef held, or NULL if the
 * handle is invalid or final reverse-callback teardown has begun.  The caller must release
 * ReverseCallbackRundownRef after its last adapter access.
 *
 * IRQL: any (the global list walk uses a spinlock)
 */
static PDXGKRNL_ADAPTER
DxgkpHandleToAdapter(
    _In_ HANDLE DeviceHandle)
{
    PLIST_ENTRY      Entry;
    KIRQL            OldIrql;
    PDXGKRNL_ADAPTER Adapter = NULL;

    if (DeviceHandle == NULL)
        return NULL;

    /*
     * DeviceHandle is the raw DXGKRNL_ADAPTER pointer cast to HANDLE.
     * Validate it against the global list to guard against stale handles.
     */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);

    for (Entry  = DxgkAdapterGlobalListHead.Flink;
         Entry != &DxgkAdapterGlobalListHead;
         Entry  = Entry->Flink)
    {
        PDXGKRNL_ADAPTER Candidate =
            CONTAINING_RECORD(Entry, DXGKRNL_ADAPTER, GlobalAdapterListEntry);
        if (Candidate == (PDXGKRNL_ADAPTER)DeviceHandle)
        {
            if (ExAcquireRundownProtection(&Candidate->ReverseCallbackRundownRef))
                Adapter = Candidate;
            break;
        }
    }

    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);
    return Adapter;
}

/* ========================================================================
 * DxgkCb* callbacks — exported to the miniport via DXGK_INTERFACE
 * ====================================================================== */

/*
 * DxgkCbNotifyInterrupt
 *
 * Called from the miniport's ISR (at DIRQL) to publish interrupt data to
 * dxgkrnl. The miniport separately calls DxgkCbQueueDpc before leaving its
 * ISR when deferred processing is required.
 *
 * IRQL: DIRQL (called from ISR context)
 */
VOID
APIENTRY
DxgkCbNotifyInterrupt(
    _In_ HANDLE DeviceHandle,
    IN_CONST_PDXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyInterruptData)
{
    PDXGKRNL_ADAPTER Adapter;

    /*
     * DeviceHandle is set to Adapter at DxgkpFillInterface time; it is
     * valid as long as the adapter is started.  We do not walk the global
     * list here (cannot acquire a spinlock while holding another at DIRQL)
     * — cast directly.
     */
    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;
    if (Adapter == NULL)
        return;
    if (NotifyInterruptData == NULL)
        return;
    if (!DxgkpAcquireVidSchCallback(Adapter))
        return;

    InterlockedIncrement(&Adapter->NotifyTotalCount);
    if ((ULONG)NotifyInterruptData->InterruptType <
        RTL_NUMBER_OF(Adapter->NotifyInterruptTypeCount))
    {
        InterlockedIncrement(
            &Adapter->NotifyInterruptTypeCount[NotifyInterruptData->InterruptType]);
    }
    if (NotifyInterruptData->InterruptType == DXGK_INTERRUPT_DMA_COMPLETED)
    {
        LARGE_INTEGER CompletionCounter;

        InterlockedExchange64(&Adapter->LastDmaNotifyTime100ns, (LONG64)DxgkDiagNow100ns());
        CompletionCounter = KeQueryPerformanceCounter(NULL);
        InterlockedExchange64(&Adapter->LastDmaCompletedCounter,
                              CompletionCounter.QuadPart);
        InterlockedExchange(&Adapter->LastDmaCompletedFence,
                            (LONG)NotifyInterruptData->DmaCompleted.SubmissionFenceId);
    }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    else if (NotifyInterruptData->InterruptType ==
             DXGK_INTERRUPT_DMA_PAGE_FAULTED)
    {
        ULONG FaultErrorCode;

        FaultErrorCode =
            (NotifyInterruptData->DmaPageFaulted.FaultErrorCode.
                 IsDeviceSpecificCode ? 1u : 0u) |
            (NotifyInterruptData->DmaPageFaulted.FaultErrorCode.
                 DeviceSpecificCode << 1);
        InterlockedExchange(
            &Adapter->LastPageFaultFence,
            (LONG)NotifyInterruptData->DmaPageFaulted.FaultedFenceId);
        InterlockedExchange64(
            &Adapter->LastPageFaultPrimitiveSequence,
            (LONGLONG)NotifyInterruptData->DmaPageFaulted.
                FaultedPrimitiveAPISequenceNumber);
        InterlockedExchange(
            &Adapter->LastPageFaultPipelineStage,
            (LONG)NotifyInterruptData->DmaPageFaulted.FaultedPipelineStage);
        InterlockedExchange(
            &Adapter->LastPageFaultBindTableEntry,
            (LONG)NotifyInterruptData->DmaPageFaulted.FaultedBindTableEntry);
        InterlockedExchange(
            &Adapter->LastPageFaultFlags,
            (LONG)NotifyInterruptData->DmaPageFaulted.PageFaultFlags);
        InterlockedExchange64(
            &Adapter->LastPageFaultVirtualAddress,
            (LONGLONG)NotifyInterruptData->DmaPageFaulted.
                FaultedVirtualAddress);
        InterlockedExchange(
            &Adapter->LastPageFaultNode,
            (LONG)NotifyInterruptData->DmaPageFaulted.NodeOrdinal);
        InterlockedExchange(
            &Adapter->LastPageFaultEngine,
            (LONG)NotifyInterruptData->DmaPageFaulted.EngineOrdinal);
        InterlockedExchange(
            &Adapter->LastPageFaultLevel,
            (LONG)NotifyInterruptData->DmaPageFaulted.PageTableLevel);
        InterlockedExchange(
            &Adapter->LastPageFaultErrorCode,
            (LONG)FaultErrorCode);
        InterlockedExchange64(
            &Adapter->LastPageFaultProcessHandle,
            (LONG64)(LONG_PTR)NotifyInterruptData->DmaPageFaulted.
                FaultedProcessHandle);
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    if (NotifyInterruptData->InterruptType ==
        DXGK_INTERRUPT_MONITORED_FENCE_SIGNALED)
    {
        (VOID)DxgkpQueueMonitoredFenceEvaluation(
            Adapter,
            NotifyInterruptData->MonitoredFenceSignaled.NodeOrdinal,
            NotifyInterruptData->MonitoredFenceSignaled.EngineOrdinal);
    }
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    if (NotifyInterruptData->InterruptType ==
        DXGK_INTERRUPT_PERIODIC_MONITORED_FENCE_SIGNALED)
    {
        BOOLEAN QueuePeriodicDpc;
        KIRQL OldIrql;

        if (DxgkpPeriodicInterruptHandoffSupported(Adapter))
        {
            OldIrql = DxgkpAcquireAdapterInterruptLock(Adapter);
            (VOID)DxgkPeriodicInterruptCoreEnqueueLocked(&Adapter->PeriodicInterruptCore, NotifyInterruptData->PeriodicMonitoredFenceSignaled.VidPnTargetId, NotifyInterruptData->PeriodicMonitoredFenceSignaled.NotificationID, 1, &QueuePeriodicDpc);
            DxgkpReleaseAdapterInterruptLock(Adapter, OldIrql);
        }
    }
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    if (NotifyInterruptData->InterruptType == DXGK_INTERRUPT_NATIVE_FENCE_SIGNALED &&
        Adapter->MiniportContext != NULL &&
        DxgkCapsCoreInterfaceVersionAtLeast(Adapter->MiniportContext->InitData.s.Version, DXGK_CAPS_CORE_LEVEL_WDDM_3_2) &&
        NotifyInterruptData->Flags.EvaluateLegacyMonitoredFences)
    {
        (VOID)DxgkpQueueMonitoredFenceEvaluation(Adapter, NotifyInterruptData->NativeFenceSignaled.NodeOrdinal, NotifyInterruptData->NativeFenceSignaled.EngineOrdinal);
    }
#endif
    if (NotifyInterruptData->InterruptType == DXGK_INTERRUPT_CRTC_VSYNC)
    {
        ULONG TargetId = NotifyInterruptData->CrtcVsync.VidPnTargetId;

        if (TargetId < Adapter->PresentQueueCount && TargetId < 32)
            InterlockedOr(&Adapter->VsyncPending, (LONG)(1UL << TargetId));
    }

    VidSchNotifyInterrupt(Adapter, NotifyInterruptData);
    DxgkpReleaseVidSchCallback(Adapter);
}

/*
 * DxgkCbNotifyDpc
 *
 * Called from the miniport's DPC routine to signal that its deferred
 * interrupt processing is complete.
 *
 * IRQL: DISPATCH_LEVEL
 */
VOID
APIENTRY
DxgkCbNotifyDpc(
    _In_ HANDLE DeviceHandle)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;
    if (Adapter == NULL)
        return;
    if (!DxgkpAcquireVidSchCallback(Adapter))
        return;

    VidSchNotifyDpc(Adapter);
    DxgkpReleaseVidSchCallback(Adapter);
}

static VOID
DxgkpFreeAdapterRegistryPath(
    _Inout_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter->DeviceRegistryPath.Buffer != NULL)
    {
        ExFreePoolWithTag(Adapter->DeviceRegistryPath.Buffer,
                          TAG_DXGK_REGISTRY);
    }
    RtlZeroMemory(&Adapter->DeviceRegistryPath,
                  sizeof(Adapter->DeviceRegistryPath));
}

/*
 * Cache the full name of the PDO's software key for
 * DXGK_DEVICE_INFO.DeviceRegistryPath.  The miniport DriverEntry registry
 * path names Services\\<service>; WDDM requires the distinct PnP software
 * key at Control\\Class\\{ClassGuid}\\NNNN.
 */
static NTSTATUS
DxgkpInitializeAdapterRegistryPath(
    _Inout_ PDXGKRNL_ADAPTER Adapter)
{
    HANDLE KeyHandle = NULL;
    PKEY_NAME_INFORMATION KeyName = NULL;
    PWCHAR PathBuffer = NULL;
    ULONG RequiredLength = 0;
    ULONG PathBytes;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = IoOpenDeviceRegistryKey(Adapter->PhysicalDeviceObject,
                                     PLUGPLAY_REGKEY_DRIVER,
                                     KEY_READ,
                                     &KeyHandle);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = ZwQueryKey(KeyHandle,
                        KeyNameInformation,
                        NULL,
                        0,
                        &RequiredLength);
    if (Status != STATUS_BUFFER_TOO_SMALL &&
        Status != STATUS_BUFFER_OVERFLOW)
    {
        if (NT_SUCCESS(Status))
            Status = STATUS_DATA_ERROR;
        goto Cleanup;
    }

    if (RequiredLength < FIELD_OFFSET(KEY_NAME_INFORMATION, Name))
    {
        Status = STATUS_DATA_ERROR;
        goto Cleanup;
    }

    KeyName = ExAllocatePoolWithTag(PagedPool,
                                    RequiredLength,
                                    TAG_DXGK_REGISTRY);
    if (KeyName == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = ZwQueryKey(KeyHandle,
                        KeyNameInformation,
                        KeyName,
                        RequiredLength,
                        &RequiredLength);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if ((KeyName->NameLength & (sizeof(WCHAR) - 1)) != 0 ||
        KeyName->NameLength > UNICODE_STRING_MAX_BYTES - sizeof(WCHAR))
    {
        Status = STATUS_NAME_TOO_LONG;
        goto Cleanup;
    }

    PathBytes = KeyName->NameLength + sizeof(WCHAR);
    PathBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                       PathBytes,
                                       TAG_DXGK_REGISTRY);
    if (PathBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    if (KeyName->NameLength != 0)
    {
        RtlCopyMemory(PathBuffer,
                      KeyName->Name,
                      KeyName->NameLength);
    }
    PathBuffer[KeyName->NameLength / sizeof(WCHAR)] = UNICODE_NULL;

    Adapter->DeviceRegistryPath.Buffer = PathBuffer;
    Adapter->DeviceRegistryPath.Length = (USHORT)KeyName->NameLength;
    Adapter->DeviceRegistryPath.MaximumLength = (USHORT)PathBytes;
    PathBuffer = NULL;
    Status = STATUS_SUCCESS;

Cleanup:
    if (PathBuffer != NULL)
        ExFreePoolWithTag(PathBuffer, TAG_DXGK_REGISTRY);
    if (KeyName != NULL)
        ExFreePoolWithTag(KeyName, TAG_DXGK_REGISTRY);
    if (KeyHandle != NULL)
        ZwClose(KeyHandle);
    return Status;
}

/*
 * DxgkCbGetDeviceInformation
 *
 * Fills in DXGK_DEVICE_INFO for the miniport.  Called during
 * DxgkDdiStartDevice before the miniport probes hardware.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbGetDeviceInformation(
    _In_  HANDLE            DeviceHandle,
    _Out_ PDXGK_DEVICE_INFO DeviceInformation)
{
    PDXGKRNL_ADAPTER Adapter;
    PPHYSICAL_MEMORY_RANGE MemoryRanges;
    PPHYSICAL_MEMORY_RANGE Range;
    ULONGLONG TotalSystemMemory = 0;
    ULONGLONG HighestPhysicalAddress = 0;
    BOOLEAN FoundPhysicalRange = FALSE;

    PAGED_CODE();

    if (DeviceInformation == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(DeviceInformation, sizeof(*DeviceInformation));

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    DeviceInformation->MiniportDeviceContext = Adapter->MiniportDeviceContext;
    DeviceInformation->PhysicalDeviceObject = Adapter->PhysicalDeviceObject;
    DeviceInformation->DeviceRegistryPath = Adapter->DeviceRegistryPath;
    DeviceInformation->TranslatedResourceList = Adapter->TranslatedResources;

    {
        /* Report the resource list once per adapter: a miniport that rejects
         * start with an invalid-parameter status is usually objecting to what
         * it finds here rather than to the start arguments themselves.  This
         * must not be a single global latch -- with two adapters present the
         * first one to start would consume it and hide the second. */
        static PVOID ReportedAdapters[8];
        static LONG ReportedCount = 0;
        BOOLEAN AlreadyReported = FALSE;
        LONG Slot;

        for (Slot = 0; Slot < ReportedCount && Slot < (LONG)RTL_NUMBER_OF(ReportedAdapters); ++Slot)
        {
            if (ReportedAdapters[Slot] == Adapter)
            {
                AlreadyReported = TRUE;
                break;
            }
        }
        if (!AlreadyReported && Adapter->TranslatedResources != NULL)
        {
            ULONG ListIndex;

            Slot = InterlockedIncrement(&ReportedCount) - 1;
            if (Slot >= 0 && Slot < (LONG)RTL_NUMBER_OF(ReportedAdapters))
                ReportedAdapters[Slot] = Adapter;

            for (ListIndex = 0;
                 ListIndex < Adapter->TranslatedResources->Count;
                 ++ListIndex)
            {
                PCM_PARTIAL_RESOURCE_LIST Partial =
                    &Adapter->TranslatedResources->List[ListIndex].PartialResourceList;
                ULONG Index;

                for (Index = 0; Index < Partial->Count; ++Index)
                {
                    PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc =
                        &Partial->PartialDescriptors[Index];
                    ULONGLONG Decoded = Desc->u.Memory.Length;

                    /* CmResourceTypeMemoryLarge stores a scaled length in a
                     * different union member for each granularity. */
                    if (Desc->Type == CmResourceTypeMemoryLarge)
                    {
                        if (Desc->Flags & CM_RESOURCE_MEMORY_LARGE_40)
                            Decoded = (ULONGLONG)Desc->u.Memory40.Length40 << 8;
                        else if (Desc->Flags & CM_RESOURCE_MEMORY_LARGE_48)
                            Decoded = (ULONGLONG)Desc->u.Memory48.Length48 << 16;
                        else if (Desc->Flags & CM_RESOURCE_MEMORY_LARGE_64)
                            Decoded = (ULONGLONG)Desc->u.Memory64.Length64 << 32;
                    }
                    DXGKRNL_INFO("adapter %p resource[%lu.%lu] type=%u flags=0x%04x "
                                "start=0x%I64x rawlen=0x%08lx len=0x%I64x\n",
                                Adapter, ListIndex, Index, Desc->Type, Desc->Flags,
                                Desc->u.Memory.Start.QuadPart,
                                Desc->u.Memory.Length,
                                Decoded);
                }
            }
        }
    }
    DeviceInformation->DockingState = DockStateUnsupported;

    /*
     * HighestPhysicalAddress is the highest byte in any installed physical
     * run, not merely (installed-page-count - 1).  Machines with firmware
     * holes make those values observably different.
     */
    MemoryRanges = MmGetPhysicalMemoryRanges();
    if (MemoryRanges != NULL)
    {
        for (Range = MemoryRanges;
             Range->BaseAddress.QuadPart != 0 ||
                 Range->NumberOfBytes.QuadPart != 0;
             ++Range)
        {
            ULONGLONG BaseAddress;
            ULONGLONG NumberOfBytes;
            ULONGLONG EndAddress;

            if (Range->BaseAddress.QuadPart < 0 ||
                Range->NumberOfBytes.QuadPart <= 0)
                continue;

            BaseAddress = (ULONGLONG)Range->BaseAddress.QuadPart;
            NumberOfBytes = (ULONGLONG)Range->NumberOfBytes.QuadPart;
            TotalSystemMemory =
                TotalSystemMemory > MAXULONGLONG - NumberOfBytes ?
                    MAXULONGLONG :
                    TotalSystemMemory + NumberOfBytes;
            EndAddress = BaseAddress > MAXULONGLONG - (NumberOfBytes - 1) ?
                MAXULONGLONG :
                BaseAddress + NumberOfBytes - 1;
            if (!FoundPhysicalRange ||
                EndAddress > HighestPhysicalAddress)
                HighestPhysicalAddress = EndAddress;
            FoundPhysicalRange = TRUE;
        }
        ExFreePool(MemoryRanges);
    }

    if (!FoundPhysicalRange)
    {
        ULONGLONG NumberOfPhysicalPages =
            SharedUserData->NumberOfPhysicalPages;

        TotalSystemMemory =
            NumberOfPhysicalPages >
                ((ULONGLONG)MAXLONGLONG >> PAGE_SHIFT) ?
                (ULONGLONG)MAXLONGLONG :
                NumberOfPhysicalPages << PAGE_SHIFT;
        if (TotalSystemMemory != 0)
            HighestPhysicalAddress = TotalSystemMemory - 1;
    }

    DeviceInformation->SystemMemorySize.QuadPart =
        (LONGLONG)min(TotalSystemMemory, (ULONGLONG)MAXLONGLONG);
    DeviceInformation->HighestPhysicalAddress.QuadPart =
        (LONGLONG)min(HighestPhysicalAddress, (ULONGLONG)MAXLONGLONG);

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

/*
 * DxgkCbAllocateContiguousMemory
 *
 * Allocates a physically contiguous block of memory on behalf of the
 * miniport.  Wraps MmAllocateContiguousMemorySpecifyCache.
 *
 * IRQL: PASSIVE_LEVEL
 */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
static NTSTATUS
DxgkpBuildMdlForContiguousAllocation(
    _In_ PVOID VirtualAddress,
    _In_ SIZE_T Size,
    _Out_ PMDL *Mdl)
{
    PPFN_NUMBER Pages;
    ULONG PageCount;
    ULONG i;
    PMDL NewMdl;

    if (Size == 0 || Size > MAXULONG)
        return STATUS_INVALID_PARAMETER;

    /*
     * MmAllocatePagesForMdl returns an Ex-pool MDL whose storage the caller
     * releases after the matching page-free operation.  Use MmCreateMdl
     * rather than IoAllocateMdl so small MDLs do not come from the I/O
     * lookaside list and remain compatible with that ownership contract.
     */
    NewMdl = MmCreateMdl(NULL, VirtualAddress, Size);
    if (NewMdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /*
     * The returned MDL is a page-list allocation contract, not a description
     * of the allocator's existing kernel VA.  In particular, do not use
     * MmBuildMdlForNonPagedPool here: that sets
     * MDL_SOURCE_IS_NONPAGED_POOL and makes a later
     * MmMapLockedPagesSpecifyCache call invalid.  Populate the PFNs while
     * retaining the contiguous allocation separately as the backing owner.
     */
    NewMdl->Process = NULL;
    NewMdl->MappedSystemVa = NULL;
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(VirtualAddress, Size);
    Pages = MmGetMdlPfnArray(NewMdl);
    for (i = 0; i < PageCount; ++i)
    {
        PHYSICAL_ADDRESS PhysicalAddress =
            MmGetPhysicalAddress((PUCHAR)PAGE_ALIGN(VirtualAddress) +
                                 ((SIZE_T)i << PAGE_SHIFT));

        Pages[i] = (PFN_NUMBER)
            ((ULONGLONG)PhysicalAddress.QuadPart >> PAGE_SHIFT);
    }

    NewMdl->MdlFlags |= MDL_PAGES_LOCKED;
    *Mdl = NewMdl;
    return STATUS_SUCCESS;
}

static VOID
DxgkpFreeCallbackMemoryEntry(
    _In_ PDXGKP_CALLBACK_MEMORY_ENTRY Entry,
    _In_ BOOLEAN FreeMdlStorage)
{
    PMDL Mdl = NULL;

    switch (Entry->Kind)
    {
        case DxgkpCallbackMemoryContiguous:
            MmFreeContiguousMemory(Entry->Memory.ContiguousMemory);
            break;

        case DxgkpCallbackMemoryMdl:
            Mdl = Entry->Memory.Mdl;
            MmFreePagesFromMdl(Mdl);
            break;

        case DxgkpCallbackMemoryContiguousMdl:
        {
            PPFN_NUMBER Pages;
            ULONG PageCount;

            Mdl = Entry->Memory.ContiguousMdl.Mdl;
            if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
                MmUnmapLockedPages(Mdl->MappedSystemVa, Mdl);

            PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(
                            (PVOID)((ULONG_PTR)Mdl->StartVa +
                                    Mdl->ByteOffset),
                            Mdl->ByteCount);
            Pages = MmGetMdlPfnArray(Mdl);
            while (PageCount-- != 0)
                *Pages++ = (PFN_NUMBER)-1;
            Mdl->MdlFlags &= ~MDL_PAGES_LOCKED;

            MmFreeContiguousMemory(
                Entry->Memory.ContiguousMdl.BaseAddress);
            break;
        }

        default:
            ASSERT(FALSE);
            break;
    }

    /*
     * DxgkCbFreePagesFromMdl releases the pages tracked by hMemoryHandle, but
     * the pMdl storage returned to the miniport remains its responsibility.
     * Keep complete destruction for adapter-removal reclamation, where the
     * miniport can no longer release a leaked descriptor itself.
     */
    if (Mdl != NULL && FreeMdlStorage)
        ExFreePool(Mdl);

    ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
}

static PDXGKP_CALLBACK_MEMORY_ENTRY
DxgkpDetachCallbackMemoryEntry(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE MemoryHandle,
    _In_ DXGKP_CALLBACK_MEMORY_KIND Kind)
{
    PLIST_ENTRY Link;
    PDXGKP_CALLBACK_MEMORY_ENTRY Found = NULL;

    ExAcquireFastMutex(&DxgkpCallbackMemoryMutex);
    for (Link = DxgkpCallbackMemoryList.Flink;
         Link != &DxgkpCallbackMemoryList;
         Link = Link->Flink)
    {
        PDXGKP_CALLBACK_MEMORY_ENTRY Entry =
            CONTAINING_RECORD(Link, DXGKP_CALLBACK_MEMORY_ENTRY, ListEntry);

        if ((HANDLE)Entry == MemoryHandle &&
            Entry->Adapter == Adapter &&
            Entry->Kind == Kind)
        {
            RemoveEntryList(&Entry->ListEntry);
            Found = Entry;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkpCallbackMemoryMutex);

    return Found;
}

static VOID
DxgkpReleaseCallbackMemory(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY FreeList;
    PLIST_ENTRY Link;
    ULONG LeakCount = 0;

    InitializeListHead(&FreeList);

    ExAcquireFastMutex(&DxgkpCallbackMemoryMutex);
    Link = DxgkpCallbackMemoryList.Flink;
    while (Link != &DxgkpCallbackMemoryList)
    {
        PLIST_ENTRY Next = Link->Flink;
        PDXGKP_CALLBACK_MEMORY_ENTRY Entry =
            CONTAINING_RECORD(Link, DXGKP_CALLBACK_MEMORY_ENTRY, ListEntry);

        if (Entry->Adapter == Adapter)
        {
            RemoveEntryList(Link);
            InsertTailList(&FreeList, Link);
            LeakCount++;
        }
        Link = Next;
    }
    ExReleaseFastMutex(&DxgkpCallbackMemoryMutex);

    while (!IsListEmpty(&FreeList))
    {
        Link = RemoveHeadList(&FreeList);
        DxgkpFreeCallbackMemoryEntry(
            CONTAINING_RECORD(Link,
                              DXGKP_CALLBACK_MEMORY_ENTRY,
                              ListEntry),
            TRUE);
    }

    if (LeakCount != 0)
    {
        DXGKRNL_WARN("DxgkpReleaseCallbackMemory: reclaimed %lu leaked "
                     "WDDM callback allocations for adapter %p\n",
                     LeakCount,
                     Adapter);
    }
}

NTSTATUS
APIENTRY
DxgkCbAllocateContiguousMemory(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARGCB_ALLOCATECONTIGUOUSMEMORY pAllocateContiguousMemory)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_CALLBACK_MEMORY_ENTRY Entry;
    MEMORY_CACHING_TYPE CacheType;
    PVOID Va;
    ULONGLONG        TotalStart100ns;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    if (pAllocateContiguousMemory == NULL)
        return STATUS_INVALID_PARAMETER;

    pAllocateContiguousMemory->hMemoryHandle = NULL;
    pAllocateContiguousMemory->pMemory = NULL;

    if (pAllocateContiguousMemory->NumberOfBytes == 0 ||
        pAllocateContiguousMemory->LowestAcceptableAddress.QuadPart < 0 ||
        pAllocateContiguousMemory->HighestAcceptableAddress.QuadPart < 0 ||
        pAllocateContiguousMemory->BoundaryAddressMultiple.QuadPart < 0 ||
        (ULONGLONG)
            pAllocateContiguousMemory->LowestAcceptableAddress.QuadPart >
        (ULONGLONG)
            pAllocateContiguousMemory->HighestAcceptableAddress.QuadPart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pAllocateContiguousMemory->BoundaryAddressMultiple.QuadPart != 0)
    {
        ULONGLONG Boundary =
            (ULONGLONG)
                pAllocateContiguousMemory->BoundaryAddressMultiple.QuadPart;

        if (Boundary < PAGE_SIZE ||
            (Boundary & (Boundary - 1)) != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    switch (pAllocateContiguousMemory->CacheType)
    {
        case DXGK_MEMORY_CACHING_TYPE_NON_CACHED:
            CacheType = MmNonCached;
            break;
        case DXGK_MEMORY_CACHING_TYPE_CACHED:
            CacheType = MmCached;
            break;
        case DXGK_MEMORY_CACHING_TYPE_WRITE_COMBINED:
            CacheType = MmWriteCombined;
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Entry),
                                  TAG_DXGK_RESOURCES);
    if (Entry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Va = MmAllocateContiguousMemorySpecifyCache(
             pAllocateContiguousMemory->NumberOfBytes,
             pAllocateContiguousMemory->LowestAcceptableAddress,
             pAllocateContiguousMemory->HighestAcceptableAddress,
             pAllocateContiguousMemory->BoundaryAddressMultiple,
             CacheType);

    if (Va == NULL)
    {
        DXGKRNL_ERR("DxgkCbAllocateContiguousMemory: failed "
                    "NumberOfBytes=%Iu\n",
                    pAllocateContiguousMemory->NumberOfBytes);
        ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Entry->Adapter = Adapter;
    Entry->Kind = DxgkpCallbackMemoryContiguous;
    Entry->Memory.ContiguousMemory = Va;

    ExAcquireFastMutex(&DxgkpCallbackMemoryMutex);
    InsertTailList(&DxgkpCallbackMemoryList, &Entry->ListEntry);
    ExReleaseFastMutex(&DxgkpCallbackMemoryMutex);

    pAllocateContiguousMemory->hMemoryHandle = (HANDLE)Entry;
    pAllocateContiguousMemory->pMemory = Va;

    DXGKRNL_TRACE("DxgkCbAllocateContiguousMemory: VA=%p "
                  "NumberOfBytes=%Iu total=%I64u us\n",
                  Va,
                  pAllocateContiguousMemory->NumberOfBytes,
                  DxgkpTraceElapsedUs(TotalStart100ns));

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

/*
 * DxgkCbFreeContiguousMemory
 *
 * Releases memory allocated by DxgkCbAllocateContiguousMemory.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbFreeContiguousMemory(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARGCB_FREECONTIGUOUSMEMORY pFreeContiguousMemory)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_CALLBACK_MEMORY_ENTRY Entry;

    PAGED_CODE();

    if (pFreeContiguousMemory == NULL ||
        pFreeContiguousMemory->hMemoryHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Entry = DxgkpDetachCallbackMemoryEntry(
                Adapter,
                pFreeContiguousMemory->hMemoryHandle,
                DxgkpCallbackMemoryContiguous);
    if (Entry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_HANDLE;
    }

    DXGKRNL_TRACE("DxgkCbFreeContiguousMemory: VA=%p\n",
                  Entry->Memory.ContiguousMemory);

    DxgkpFreeCallbackMemoryEntry(Entry, FALSE);

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

/*
 * DxgkCbAllocatePagesForMdl
 *
 * Provides the WDDM 2.4 MmAllocatePagesForMdlEx-equivalent allocation
 * service.  ReactOS does not yet expose a graphics IOMMU domain, so the
 * IOMMU map/unmap callbacks are explicit unavailable boundaries and adapters
 * continue to report the IOMMU capability as unsupported.  On that truthful
 * identity-domain path, the MDL returned here describes the physical pages
 * allocated by Mm.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbAllocatePagesForMdl(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARGCB_ALLOCATEPAGESFORMDL pAllocatePagesForMdl)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_CALLBACK_MEMORY_ENTRY Entry;
    PHYSICAL_ADDRESS HighAddress;
    MEMORY_CACHING_TYPE CacheType;
    PHYSICAL_ADDRESS BoundaryAddress;
    SIZE_T RequiredBytes;
    ULONG MmFlags;
    BOOLEAN UseContiguousMdl;
    PVOID ContiguousVa;
    PVOID ZeroVa;
    BOOLEAN UnmapZeroVa;
    PMDL Mdl;

    PAGED_CODE();

    if (pAllocatePagesForMdl == NULL)
        return STATUS_INVALID_PARAMETER;

    pAllocatePagesForMdl->hMemoryHandle = NULL;
    pAllocatePagesForMdl->pMdl = NULL;

    if (pAllocatePagesForMdl->TotalBytes == 0 ||
        pAllocatePagesForMdl->TotalBytes >
            ((SIZE_T)MAXULONG - (PAGE_SIZE - 1)) ||
        pAllocatePagesForMdl->LowAddress.QuadPart < 0 ||
        pAllocatePagesForMdl->HighAddress.QuadPart < 0 ||
        pAllocatePagesForMdl->SkipBytes.QuadPart < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pAllocatePagesForMdl->Flags &
        ~(MM_DONT_ZERO_ALLOCATION |
          MM_ALLOCATE_FROM_LOCAL_NODE_ONLY |
          MM_ALLOCATE_FULLY_REQUIRED |
          MM_ALLOCATE_NO_WAIT |
          MM_ALLOCATE_PREFER_CONTIGUOUS |
          MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS |
          MM_ALLOCATE_FAST_LARGE_PAGES |
          MM_ALLOCATE_TRIM_IF_NECESSARY |
          MM_ALLOCATE_AND_HOT_REMOVE))
    {
        return STATUS_INVALID_PARAMETER;
    }

    UseContiguousMdl =
        (pAllocatePagesForMdl->Flags &
         MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS) != 0;

    if (pAllocatePagesForMdl->SkipBytes.QuadPart != 0)
    {
        ULONGLONG SkipBytes =
            (ULONGLONG)pAllocatePagesForMdl->SkipBytes.QuadPart;

        if (SkipBytes < PAGE_SIZE ||
            (SkipBytes & (PAGE_SIZE - 1)) != 0 ||
            (UseContiguousMdl && (SkipBytes & (SkipBytes - 1)) != 0))
        {
            return STATUS_INVALID_PARAMETER;
        }

        /*
         * The current contiguous allocator can provide one aligned chunk.
         * A multi-chunk MDL needs separate backing-allocation tracking and is
         * not advertised until that ownership model exists.
         */
        if (!UseContiguousMdl)
            return STATUS_NOT_SUPPORTED;
        if ((pAllocatePagesForMdl->TotalBytes % SkipBytes) != 0)
            return STATUS_INVALID_PARAMETER;
        if (pAllocatePagesForMdl->TotalBytes != SkipBytes)
            return STATUS_NOT_SUPPORTED;
    }

    /*
     * Large-page-cache and hot-remove allocations are hard requirements that
     * ReactOS Mm cannot yet honor.  PREFER_CONTIGUOUS and TRIM_IF_NECESSARY
     * remain best-effort preferences, while NO_WAIT is already satisfied by
     * the nonblocking allocator.
     */
    if (pAllocatePagesForMdl->Flags &
        (MM_ALLOCATE_FAST_LARGE_PAGES |
         MM_ALLOCATE_AND_HOT_REMOVE))
    {
        return STATUS_NOT_SUPPORTED;
    }

    switch (pAllocatePagesForMdl->CacheType)
    {
        case DXGK_MEMORY_CACHING_TYPE_NON_CACHED:
            CacheType = MmNonCached;
            break;
        case DXGK_MEMORY_CACHING_TYPE_CACHED:
            CacheType = MmCached;
            break;
        case DXGK_MEMORY_CACHING_TYPE_WRITE_COMBINED:
            CacheType = MmWriteCombined;
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }

    HighAddress = pAllocatePagesForMdl->HighAddress;
    if ((ULONGLONG)pAllocatePagesForMdl->LowAddress.QuadPart >
        (ULONGLONG)HighAddress.QuadPart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Entry),
                                  TAG_DXGK_RESOURCES);
    if (Entry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * ReactOS Mm accepts only the two material flags below.  Enforce the
     * WDDM callback's always-fully-required rule explicitly after allocation.
     */
    ContiguousVa = NULL;
    if (UseContiguousMdl)
    {
        BoundaryAddress = pAllocatePagesForMdl->SkipBytes;
        ContiguousVa = MmAllocateContiguousMemorySpecifyCache(
                           pAllocatePagesForMdl->TotalBytes,
                           pAllocatePagesForMdl->LowAddress,
                           HighAddress,
                           BoundaryAddress,
                           CacheType);
        if (ContiguousVa != NULL)
        {
            if (!NT_SUCCESS(DxgkpBuildMdlForContiguousAllocation(
                                ContiguousVa,
                                pAllocatePagesForMdl->TotalBytes,
                                &Mdl)))
            {
                MmFreeContiguousMemory(ContiguousVa);
                ContiguousVa = NULL;
                Mdl = NULL;
            }
        }
        else
        {
            Mdl = NULL;
        }
    }
    else
    {
        MmFlags = pAllocatePagesForMdl->Flags &
                  (MM_DONT_ZERO_ALLOCATION |
                   MM_ALLOCATE_FROM_LOCAL_NODE_ONLY);
        Mdl = MmAllocatePagesForMdlEx(
                  pAllocatePagesForMdl->LowAddress,
                  HighAddress,
                  pAllocatePagesForMdl->SkipBytes,
                  pAllocatePagesForMdl->TotalBytes,
                  CacheType,
                  MmFlags);
    }
    if (Mdl == NULL)
    {
        ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RequiredBytes =
        (pAllocatePagesForMdl->TotalBytes + PAGE_SIZE - 1) &
        ~((SIZE_T)PAGE_SIZE - 1);
    if ((SIZE_T)Mdl->ByteCount < RequiredBytes)
    {
        if (UseContiguousMdl)
        {
            IoFreeMdl(Mdl);
            MmFreeContiguousMemory(ContiguousVa);
        }
        else
        {
            MmFreePagesFromMdl(Mdl);
            ExFreePool(Mdl);
        }
        ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * MmAllocatePagesForMdlEx guarantees zero-filled pages unless the caller
     * explicitly supplies MM_DONT_ZERO_ALLOCATION.  Enforce that public
     * contract at the Dxgkrnl boundary as well: display miniports commonly
     * build firmware descriptors in place and may only OR flag fields into
     * the fresh allocation.  Returning stale contents therefore changes the
     * GPU ABI, in addition to leaking data across allocations.
     */
    if (!(pAllocatePagesForMdl->Flags & MM_DONT_ZERO_ALLOCATION))
    {
        UnmapZeroVa = FALSE;
        if (UseContiguousMdl)
        {
            ZeroVa = ContiguousVa;
        }
        else
        {
            ZeroVa = MmMapLockedPagesSpecifyCache(
                         Mdl,
                         KernelMode,
                         CacheType,
                         NULL,
                         FALSE,
                         NormalPagePriority | MdlMappingNoExecute);
            UnmapZeroVa = (ZeroVa != NULL);
        }

        if (ZeroVa == NULL)
        {
            if (UseContiguousMdl)
            {
                IoFreeMdl(Mdl);
                MmFreeContiguousMemory(ContiguousVa);
            }
            else
            {
                MmFreePagesFromMdl(Mdl);
                ExFreePool(Mdl);
            }
            ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(ZeroVa, RequiredBytes);
        if (UnmapZeroVa)
            MmUnmapLockedPages(ZeroVa, Mdl);
    }

    Entry->Adapter = Adapter;
    if (UseContiguousMdl)
    {
        Entry->Kind = DxgkpCallbackMemoryContiguousMdl;
        Entry->Memory.ContiguousMdl.BaseAddress = ContiguousVa;
        Entry->Memory.ContiguousMdl.Mdl = Mdl;
    }
    else
    {
        Entry->Kind = DxgkpCallbackMemoryMdl;
        Entry->Memory.Mdl = Mdl;
    }

    ExAcquireFastMutex(&DxgkpCallbackMemoryMutex);
    InsertTailList(&DxgkpCallbackMemoryList, &Entry->ListEntry);
    ExReleaseFastMutex(&DxgkpCallbackMemoryMutex);

    pAllocatePagesForMdl->hMemoryHandle = (HANDLE)Entry;
    pAllocatePagesForMdl->pMdl = Mdl;

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

/*
 * DxgkCbFreePagesFromMdl
 *
 * Releases the pages tracked by the matching allocation callback.  The
 * miniport retains ownership of the returned pMdl storage and releases it
 * after this callback succeeds.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbFreePagesFromMdl(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARGCB_FREEPAGESFROMMDL pFreePagesFromMdl)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_CALLBACK_MEMORY_ENTRY Entry;

    PAGED_CODE();

    if (pFreePagesFromMdl == NULL ||
        pFreePagesFromMdl->hMemoryHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Entry = DxgkpDetachCallbackMemoryEntry(
                Adapter,
                pFreePagesFromMdl->hMemoryHandle,
                DxgkpCallbackMemoryMdl);
    if (Entry == NULL)
    {
        Entry = DxgkpDetachCallbackMemoryEntry(
                    Adapter,
                    pFreePagesFromMdl->hMemoryHandle,
                    DxgkpCallbackMemoryContiguousMdl);
    }
    if (Entry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_HANDLE;
    }

    DxgkpFreeCallbackMemoryEntry(Entry, FALSE);

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
static NTSTATUS
DxgkpConvertPhysicalCacheType(
    _In_ DXGK_MEMORY_CACHING_TYPE DxgkCacheType,
    _Out_ MEMORY_CACHING_TYPE *MmCacheType)
{
    switch (DxgkCacheType)
    {
        case DXGK_MEMORY_CACHING_TYPE_NON_CACHED:
            *MmCacheType = MmNonCached;
            return STATUS_SUCCESS;
        case DXGK_MEMORY_CACHING_TYPE_CACHED:
            *MmCacheType = MmCached;
            return STATUS_SUCCESS;
        case DXGK_MEMORY_CACHING_TYPE_WRITE_COMBINED:
            *MmCacheType = MmWriteCombined;
            return STATUS_SUCCESS;
        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static PDXGKP_PHYSICAL_MEMORY_OBJECT
DxgkpFindPhysicalMemoryObjectLocked(
    _In_ HANDLE Handle)
{
    PLIST_ENTRY Link;

    for (Link = DxgkpPhysicalMemoryList.Flink;
         Link != &DxgkpPhysicalMemoryList;
         Link = Link->Flink)
    {
        PDXGKP_PHYSICAL_MEMORY_OBJECT Object =
            CONTAINING_RECORD(Link,
                              DXGKP_PHYSICAL_MEMORY_OBJECT,
                              ListEntry);

        if ((HANDLE)Object == Handle)
            return Object;
    }

    return NULL;
}

static PDXGKP_ADAPTER_MEMORY_OBJECT
DxgkpFindAdapterMemoryObjectLocked(
    _In_ HANDLE Handle)
{
    PLIST_ENTRY ObjectLink;

    for (ObjectLink = DxgkpPhysicalMemoryList.Flink;
         ObjectLink != &DxgkpPhysicalMemoryList;
         ObjectLink = ObjectLink->Flink)
    {
        PDXGKP_PHYSICAL_MEMORY_OBJECT Object =
            CONTAINING_RECORD(ObjectLink,
                              DXGKP_PHYSICAL_MEMORY_OBJECT,
                              ListEntry);
        PLIST_ENTRY AdapterLink;

        for (AdapterLink = Object->AdapterMemoryList.Flink;
             AdapterLink != &Object->AdapterMemoryList;
             AdapterLink = AdapterLink->Flink)
        {
            PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject =
                CONTAINING_RECORD(AdapterLink,
                                  DXGKP_ADAPTER_MEMORY_OBJECT,
                                  ListEntry);

            if ((HANDLE)AdapterObject == Handle)
                return AdapterObject;
        }
    }

    return NULL;
}

static BOOLEAN
DxgkpAdapterMemoryObjectHasAdlLocked(
    _In_ PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject)
{
    PLIST_ENTRY Link;

    for (Link = AdapterObject->PhysicalObject->AdlList.Flink;
         Link != &AdapterObject->PhysicalObject->AdlList;
         Link = Link->Flink)
    {
        PDXGKP_ADL_ENTRY Entry =
            CONTAINING_RECORD(Link, DXGKP_ADL_ENTRY, ListEntry);

        if (Entry->AdapterMemoryObject == AdapterObject)
            return TRUE;
    }

    return FALSE;
}

static VOID
DxgkpDetachAdapterMemoryObjectAdlsLocked(
    _In_ PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject,
    _Inout_ PLIST_ENTRY ReclaimList)
{
    PLIST_ENTRY Link;

    Link = AdapterObject->PhysicalObject->AdlList.Flink;
    while (Link != &AdapterObject->PhysicalObject->AdlList)
    {
        PLIST_ENTRY Next = Link->Flink;
        PDXGKP_ADL_ENTRY Entry =
            CONTAINING_RECORD(Link, DXGKP_ADL_ENTRY, ListEntry);

        if (Entry->AdapterMemoryObject == AdapterObject)
        {
            RemoveEntryList(Link);
            InsertTailList(ReclaimList, Link);
        }
        Link = Next;
    }
}

static VOID
DxgkpFreeDetachedAdls(
    _Inout_ PLIST_ENTRY ReclaimList)
{
    while (!IsListEmpty(ReclaimList))
    {
        PLIST_ENTRY Link = RemoveHeadList(ReclaimList);

        ExFreePoolWithTag(CONTAINING_RECORD(Link, DXGKP_ADL_ENTRY, ListEntry), TAG_DXGK_RESOURCES);
    }
}

static VOID
DxgkpFreePhysicalMapping(
    _In_ PDXGKP_PHYSICAL_MAPPING Mapping)
{
    if (Mapping->Kind == DxgkpPhysicalMappingMdl)
    {
        KAPC_STATE ApcState;
        BOOLEAN Attached = FALSE;

        if (Mapping->Process != NULL &&
            Mapping->Process != PsGetCurrentProcess())
        {
            KeStackAttachProcess((PKPROCESS)Mapping->Process, &ApcState);
            Attached = TRUE;
        }
        MmUnmapLockedPages(Mapping->BaseAddress, Mapping->MappingMdl);
        if (Attached)
            KeUnstackDetachProcess(&ApcState);
        IoFreeMdl(Mapping->MappingMdl);
    }
    else if (Mapping->Kind == DxgkpPhysicalMappingIoSpace)
    {
        MmUnmapIoSpace(Mapping->BaseAddress, Mapping->Size);
    }

    if (Mapping->Process != NULL)
        ObDereferenceObject(Mapping->Process);
    ExFreePoolWithTag(Mapping, TAG_DXGK_RESOURCES);
}

static VOID
DxgkpDetachPhysicalMemoryObjectLocked(
    _In_ PDXGKP_PHYSICAL_MEMORY_OBJECT Object)
{
    RemoveEntryList(&Object->ListEntry);
}

static VOID
DxgkpFreePhysicalMemoryObject(
    _In_ PDXGKP_PHYSICAL_MEMORY_OBJECT Object)
{
    PLIST_ENTRY Link;

    while (!IsListEmpty(&Object->MappingList))
    {
        Link = RemoveHeadList(&Object->MappingList);
        DxgkpFreePhysicalMapping(
            CONTAINING_RECORD(Link,
                              DXGKP_PHYSICAL_MAPPING,
                              ListEntry));
    }

    while (!IsListEmpty(&Object->AdlList))
    {
        Link = RemoveHeadList(&Object->AdlList);
        ExFreePoolWithTag(
            CONTAINING_RECORD(Link, DXGKP_ADL_ENTRY, ListEntry),
            TAG_DXGK_RESOURCES);
    }

    while (!IsListEmpty(&Object->AdapterMemoryList))
    {
        Link = RemoveHeadList(&Object->AdapterMemoryList);
        ExFreePoolWithTag(
            CONTAINING_RECORD(Link,
                              DXGKP_ADAPTER_MEMORY_OBJECT,
                              ListEntry),
            TAG_DXGK_RESOURCES);
    }

    if (Object->Backing == DxgkpPhysicalBackingMdl)
    {
        MmFreePagesFromMdl(Object->Mdl);
        ExFreePool(Object->Mdl);
    }
    else if (Object->Backing == DxgkpPhysicalBackingContiguousMdl ||
             Object->Backing == DxgkpPhysicalBackingContiguous)
    {
        if (Object->Mdl != NULL)
            IoFreeMdl(Object->Mdl);
        MmFreeContiguousMemory(Object->VirtualAddress);
    }

    ExFreePoolWithTag(Object, TAG_DXGK_RESOURCES);
}

static VOID
DxgkpReleasePhysicalMemoryObjects(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY ObjectReclaimList;
    LIST_ENTRY AdapterObjectReclaimList;
    LIST_ENTRY AdlReclaimList;
    PLIST_ENTRY ObjectLink;
    ULONG ReclaimedObjects = 0;
    ULONG ReclaimedAdapterObjects = 0;

    InitializeListHead(&ObjectReclaimList);
    InitializeListHead(&AdapterObjectReclaimList);
    InitializeListHead(&AdlReclaimList);
    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    ObjectLink = DxgkpPhysicalMemoryList.Flink;
    while (ObjectLink != &DxgkpPhysicalMemoryList)
    {
        PLIST_ENTRY NextObjectLink = ObjectLink->Flink;
        PDXGKP_PHYSICAL_MEMORY_OBJECT Object =
            CONTAINING_RECORD(ObjectLink,
                              DXGKP_PHYSICAL_MEMORY_OBJECT,
                              ListEntry);
        if (Object->CreatorAdapter == Adapter)
        {
            DxgkpDetachPhysicalMemoryObjectLocked(Object);
            InsertTailList(&ObjectReclaimList, &Object->ListEntry);
        }
        else
        {
            PLIST_ENTRY AdapterLink = Object->AdapterMemoryList.Flink;

            while (AdapterLink != &Object->AdapterMemoryList)
            {
                PLIST_ENTRY NextAdapterLink = AdapterLink->Flink;
                PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject = CONTAINING_RECORD(AdapterLink, DXGKP_ADAPTER_MEMORY_OBJECT, ListEntry);

                if (AdapterObject->Adapter == Adapter)
                {
                    DxgkpDetachAdapterMemoryObjectAdlsLocked(AdapterObject, &AdlReclaimList);
                    RemoveEntryList(AdapterLink);
                    InsertTailList(&AdapterObjectReclaimList, AdapterLink);
                }
                AdapterLink = NextAdapterLink;
            }
        }

        ObjectLink = NextObjectLink;
    }
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);

    DxgkpFreeDetachedAdls(&AdlReclaimList);

    while (!IsListEmpty(&AdapterObjectReclaimList))
    {
        PLIST_ENTRY AdapterLink = RemoveHeadList(&AdapterObjectReclaimList);

        ExFreePoolWithTag(CONTAINING_RECORD(AdapterLink, DXGKP_ADAPTER_MEMORY_OBJECT, ListEntry), TAG_DXGK_RESOURCES);
        ReclaimedAdapterObjects++;
    }

    while (!IsListEmpty(&ObjectReclaimList))
    {
        ObjectLink = RemoveHeadList(&ObjectReclaimList);
        DxgkpFreePhysicalMemoryObject(CONTAINING_RECORD(ObjectLink, DXGKP_PHYSICAL_MEMORY_OBJECT, ListEntry));
        ReclaimedObjects++;
    }

    if (ReclaimedObjects != 0 || ReclaimedAdapterObjects != 0)
    {
        DXGKRNL_WARN("DxgkpReleasePhysicalMemoryObjects: reclaimed %lu creator-owned object(s) and closed %lu adapter view(s) for adapter %p\n", ReclaimedObjects, ReclaimedAdapterObjects, Adapter);
    }
}

NTSTATUS
APIENTRY
DxgkCbCreatePhysicalMemoryObject(
    IN_OUT_PDXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT pArgs)
{
    PDXGKP_PHYSICAL_MEMORY_OBJECT Object = NULL;
    PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject = NULL;
    PDXGKRNL_ADAPTER Adapter = NULL;
    MEMORY_CACHING_TYPE CacheType;
    NTSTATUS Status;

    PAGED_CODE();

    if (pArgs == NULL)
        return STATUS_INVALID_PARAMETER;

    pArgs->hPhysicalMemoryObject = NULL;
    pArgs->hAdapterMemoryObject = NULL;

    if (pArgs->Size == 0 || pArgs->Size > MAXULONG)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpConvertPhysicalCacheType(pArgs->CacheType, &CacheType);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pArgs->hAdapter != NULL)
    {
        Adapter = DxgkpHandleToAdapter(pArgs->hAdapter);
        if (Adapter == NULL)
            return STATUS_INVALID_HANDLE;
    }

    Object = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(*Object),
                                   TAG_DXGK_RESOURCES);
    if (Object == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }

    RtlZeroMemory(Object, sizeof(*Object));
    InitializeListHead(&Object->AdapterMemoryList);
    InitializeListHead(&Object->MappingList);
    InitializeListHead(&Object->AdlList);
    Object->CreatorAdapter = Adapter;
    Object->Type = pArgs->Type;
    Object->CacheType = pArgs->CacheType;
    Object->Size = pArgs->Size;
    Object->Context = pArgs->Context;

    if (Adapter != NULL)
    {
        AdapterObject = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(*AdapterObject),
                                              TAG_DXGK_RESOURCES);
        if (AdapterObject == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }

        AdapterObject->PhysicalObject = Object;
        AdapterObject->Adapter = Adapter;
    }

    switch (pArgs->Type)
    {
        case DXGK_PHYSICAL_MEMORY_TYPE_MDL:
        {
            ULONG AllowedFlags =
                MM_DONT_ZERO_ALLOCATION |
                MM_ALLOCATE_FROM_LOCAL_NODE_ONLY |
                MM_ALLOCATE_FULLY_REQUIRED |
                MM_ALLOCATE_NO_WAIT |
                MM_ALLOCATE_PREFER_CONTIGUOUS |
                MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS |
                MM_ALLOCATE_FAST_LARGE_PAGES |
                MM_ALLOCATE_TRIM_IF_NECESSARY |
                MM_ALLOCATE_AND_HOT_REMOVE;

            if (pArgs->Mdl.LowAddress.QuadPart < 0 ||
                pArgs->Mdl.HighAddress.QuadPart < 0 ||
                pArgs->Mdl.SkipBytes.QuadPart < 0 ||
                (ULONGLONG)pArgs->Mdl.LowAddress.QuadPart >
                    (ULONGLONG)pArgs->Mdl.HighAddress.QuadPart ||
                (pArgs->Mdl.Flags & ~AllowedFlags) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Failure;
            }

            if (pArgs->Mdl.Flags &
                (MM_ALLOCATE_FAST_LARGE_PAGES |
                 MM_ALLOCATE_AND_HOT_REMOVE))
            {
                Status = STATUS_NOT_SUPPORTED;
                goto Failure;
            }

            if (pArgs->Mdl.Flags & MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS)
            {
                ULONGLONG ChunkSize =
                    (ULONGLONG)pArgs->Mdl.SkipBytes.QuadPart;
                PHYSICAL_ADDRESS Boundary;

                if (ChunkSize != 0 &&
                    (ChunkSize < PAGE_SIZE ||
                     (ChunkSize & (ChunkSize - 1)) != 0 ||
                     (pArgs->Size % (SIZE_T)ChunkSize) != 0))
                {
                    Status = STATUS_INVALID_PARAMETER;
                    goto Failure;
                }

                /*
                 * Mm currently has no scatter/gather chunk allocator.  A
                 * single requested chunk has exact equivalent semantics when
                 * backed by its contiguous allocator.  Multi-chunk requests
                 * remain unsupported rather than returning a false contract.
                 */
                if (ChunkSize != 0 && pArgs->Size != (SIZE_T)ChunkSize)
                {
                    Status = STATUS_NOT_SUPPORTED;
                    goto Failure;
                }

                Boundary.QuadPart = (LONGLONG)ChunkSize;
                Object->VirtualAddress =
                    MmAllocateContiguousMemorySpecifyCache(
                        pArgs->Size,
                        pArgs->Mdl.LowAddress,
                        pArgs->Mdl.HighAddress,
                        Boundary,
                        CacheType);
                if (Object->VirtualAddress == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Failure;
                }

                Status = DxgkpBuildMdlForContiguousAllocation(
                             Object->VirtualAddress,
                             Object->Size,
                             &Object->Mdl);
                if (!NT_SUCCESS(Status))
                    goto Failure;

                Object->Backing = DxgkpPhysicalBackingContiguousMdl;
            }
            else
            {
                ULONG MmFlags = pArgs->Mdl.Flags &
                    (MM_DONT_ZERO_ALLOCATION |
                     MM_ALLOCATE_FROM_LOCAL_NODE_ONLY);
                SIZE_T RequiredBytes =
                    (pArgs->Size + PAGE_SIZE - 1) &
                    ~((SIZE_T)PAGE_SIZE - 1);

                Object->Mdl = MmAllocatePagesForMdlEx(
                                  pArgs->Mdl.LowAddress,
                                  pArgs->Mdl.HighAddress,
                                  pArgs->Mdl.SkipBytes,
                                  pArgs->Size,
                                  CacheType,
                                  MmFlags);
                if (Object->Mdl == NULL ||
                    (SIZE_T)Object->Mdl->ByteCount < RequiredBytes)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Failure;
                }

                Object->Backing = DxgkpPhysicalBackingMdl;
            }
            break;
        }

        case DXGK_PHYSICAL_MEMORY_TYPE_CONTIGUOUS_MEMORY:
            if (pArgs->ContiguousMemory.LowestAcceptableAddress.QuadPart < 0 ||
                pArgs->ContiguousMemory.HighestAcceptableAddress.QuadPart < 0 ||
                pArgs->ContiguousMemory.BoundaryAddressMultiple.QuadPart < 0 ||
                (ULONGLONG)pArgs->ContiguousMemory.LowestAcceptableAddress.QuadPart >
                    (ULONGLONG)pArgs->ContiguousMemory.HighestAcceptableAddress.QuadPart)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Failure;
            }

            if (pArgs->ContiguousMemory.BoundaryAddressMultiple.QuadPart != 0)
            {
                ULONGLONG Boundary =
                    (ULONGLONG)pArgs->ContiguousMemory.
                        BoundaryAddressMultiple.QuadPart;

                if (Boundary < PAGE_SIZE ||
                    (Boundary & (Boundary - 1)) != 0)
                {
                    Status = STATUS_INVALID_PARAMETER;
                    goto Failure;
                }
            }

            Object->VirtualAddress =
                MmAllocateContiguousMemorySpecifyCache(
                    pArgs->Size,
                    pArgs->ContiguousMemory.LowestAcceptableAddress,
                    pArgs->ContiguousMemory.HighestAcceptableAddress,
                    pArgs->ContiguousMemory.BoundaryAddressMultiple,
                    CacheType);
            if (Object->VirtualAddress == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Failure;
            }

            Status = DxgkpBuildMdlForContiguousAllocation(
                         Object->VirtualAddress,
                         Object->Size,
                         &Object->Mdl);
            if (!NT_SUCCESS(Status))
                goto Failure;

            Object->Backing = DxgkpPhysicalBackingContiguous;
            break;

        case DXGK_PHYSICAL_MEMORY_TYPE_IO_SPACE:
            if (pArgs->IOSpace.BaseAddress.QuadPart < 0 ||
                (pArgs->IOSpace.BaseAddress.QuadPart &
                    (PAGE_SIZE - 1)) != 0 ||
                (ULONGLONG)pArgs->IOSpace.BaseAddress.QuadPart >
                    MAXULONGLONG - ((ULONGLONG)pArgs->Size - 1))
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Failure;
            }
            Object->IoBaseAddress = pArgs->IOSpace.BaseAddress;
            Object->Backing = DxgkpPhysicalBackingIoSpace;
            break;

        case DXGK_PHYSICAL_MEMORY_TYPE_SECTION:
            Status = STATUS_NOT_SUPPORTED;
            goto Failure;

        default:
            Status = STATUS_INVALID_PARAMETER;
            goto Failure;
    }

    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    InsertTailList(&DxgkpPhysicalMemoryList, &Object->ListEntry);
    if (AdapterObject != NULL)
    {
        InsertTailList(&Object->AdapterMemoryList,
                       &AdapterObject->ListEntry);
    }
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);

    pArgs->hPhysicalMemoryObject = (HANDLE)Object;
    pArgs->hAdapterMemoryObject = (HANDLE)AdapterObject;

    if (Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;

Failure:
    if (Object != NULL)
    {
        if (Object->Backing == DxgkpPhysicalBackingMdl &&
            Object->Mdl != NULL)
        {
            MmFreePagesFromMdl(Object->Mdl);
            ExFreePool(Object->Mdl);
        }
        else if (Object->VirtualAddress != NULL)
        {
            if (Object->Mdl != NULL)
                IoFreeMdl(Object->Mdl);
            MmFreeContiguousMemory(Object->VirtualAddress);
        }
        ExFreePoolWithTag(Object, TAG_DXGK_RESOURCES);
    }
    if (AdapterObject != NULL)
        ExFreePoolWithTag(AdapterObject, TAG_DXGK_RESOURCES);
    if (Adapter != NULL)
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);

    return Status;
}

VOID
APIENTRY
DxgkCbDestroyPhysicalMemoryObject(
    IN_CONST_PDXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT pArgs)
{
    PDXGKP_PHYSICAL_MEMORY_OBJECT Object = NULL;
    BOOLEAN OutstandingAdl = FALSE;
    BOOLEAN OutstandingAdapterObject = FALSE;

    PAGED_CODE();

    if (pArgs == NULL || pArgs->hPhysicalMemoryObject == NULL)
        return;

    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    Object = DxgkpFindPhysicalMemoryObjectLocked(
                 pArgs->hPhysicalMemoryObject);
    if (Object != NULL)
    {
        if (pArgs->hAdapterMemoryObject != NULL)
        {
            PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject =
                DxgkpFindAdapterMemoryObjectLocked(
                    pArgs->hAdapterMemoryObject);

            if (AdapterObject == NULL ||
                AdapterObject->PhysicalObject != Object)
            {
                ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
                return;
            }
        }

        if (!IsListEmpty(&Object->AdlList))
        {
            OutstandingAdl = TRUE;
            Object = NULL;
        }
        else if (pArgs->hAdapterMemoryObject == NULL &&
                 !IsListEmpty(&Object->AdapterMemoryList))
        {
            OutstandingAdapterObject = TRUE;
            Object = NULL;
        }
        else
        {
            DxgkpDetachPhysicalMemoryObjectLocked(Object);
        }
    }
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);

    if (OutstandingAdl)
    {
        KeBugCheckEx(
            DXGKP_BUGCHECK_VIDEO_DXGKRNL_FATAL_ERROR,
            (ULONG_PTR)DXGKP_FATAL_PHYSICAL_MEMORY_ADL_LEAK_SUBTYPE,
            (ULONG_PTR)pArgs->hPhysicalMemoryObject,
            0,
            0);
    }
    if (OutstandingAdapterObject)
    {
        DXGKRNL_ERR("DxgkCbDestroyPhysicalMemoryObject: refusing to destroy object %p without closing its adapter view\n", pArgs->hPhysicalMemoryObject);
        return;
    }

    if (Object != NULL)
        DxgkpFreePhysicalMemoryObject(Object);
}

NTSTATUS
APIENTRY
DxgkCbMapPhysicalMemory(
    IN_OUT_PDXGKARGCB_MAP_PHYSICAL_MEMORY pArgs)
{
    PDXGKP_PHYSICAL_MEMORY_OBJECT Object;
    PDXGKP_PHYSICAL_MAPPING Mapping;
    MEMORY_CACHING_TYPE CacheType;
    SIZE_T RequestedOffset;
    SIZE_T RequestedSize;
    SIZE_T ViewOffset;
    SIZE_T OffsetInView;
    SIZE_T ViewSpan;
    SIZE_T ViewSize;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pArgs == NULL)
        return STATUS_INVALID_PARAMETER;

    pArgs->pMappedAddress = NULL;
    RequestedOffset = pArgs->Offset;
    RequestedSize = pArgs->Size;
    if (pArgs->hPhysicalMemoryObject == NULL || RequestedSize == 0 ||
        (pArgs->AccessMode != DXGK_ACCESS_MODE_KERNEL_MODE &&
         pArgs->AccessMode != DXGK_ACCESS_MODE_USER_MODE))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mapping = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Mapping),
                                    TAG_DXGK_RESOURCES);
    if (Mapping == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Mapping, sizeof(*Mapping));

    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    Object = DxgkpFindPhysicalMemoryObjectLocked(
                 pArgs->hPhysicalMemoryObject);
    if (Object == NULL || RequestedOffset > Object->Size ||
        RequestedSize > Object->Size - RequestedOffset ||
        (Object->Backing == DxgkpPhysicalBackingIoSpace &&
         pArgs->AccessMode != DXGK_ACCESS_MODE_KERNEL_MODE))
    {
        Status = Object == NULL ? STATUS_INVALID_HANDLE :
                                  STATUS_INVALID_PARAMETER;
        goto Failure;
    }

    ViewOffset = RequestedOffset & ~((SIZE_T)PAGE_SIZE - 1);
    OffsetInView = RequestedOffset - ViewOffset;
    if (RequestedSize > (SIZE_T)MAXULONG - OffsetInView)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Failure;
    }

    ViewSpan = OffsetInView + RequestedSize;
    if (ViewSpan > (SIZE_T)MAXULONG - (PAGE_SIZE - 1))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Failure;
    }
    ViewSize = (ViewSpan + PAGE_SIZE - 1) &
               ~((SIZE_T)PAGE_SIZE - 1);

    Status = DxgkpConvertPhysicalCacheType(Object->CacheType, &CacheType);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Mapping->Size = ViewSize;
    if ((Object->Backing == DxgkpPhysicalBackingContiguousMdl ||
         Object->Backing == DxgkpPhysicalBackingContiguous) &&
        pArgs->AccessMode == DXGK_ACCESS_MODE_KERNEL_MODE)
    {
        Mapping->Kind = DxgkpPhysicalMappingDirect;
        Mapping->BaseAddress =
            (PVOID)((PUCHAR)Object->VirtualAddress + ViewOffset);
    }
    else if (Object->Backing == DxgkpPhysicalBackingIoSpace)
    {
        PHYSICAL_ADDRESS Address = Object->IoBaseAddress;

        Address.QuadPart += ViewOffset;
        Mapping->Kind = DxgkpPhysicalMappingIoSpace;
        Mapping->BaseAddress = MmMapIoSpace(Address,
                                            ViewSize,
                                            CacheType);
        if (Mapping->BaseAddress == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }
    }
    else
    {
        PVOID SourceAddress =
            (PVOID)((PUCHAR)MmGetMdlVirtualAddress(Object->Mdl) +
                    ViewOffset);

        Mapping->MappingMdl = IoAllocateMdl(SourceAddress,
                                            (ULONG)ViewSize,
                                            FALSE,
                                            FALSE,
                                            NULL);
        if (Mapping->MappingMdl == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }

        IoBuildPartialMdl(Object->Mdl,
                          Mapping->MappingMdl,
                          SourceAddress,
                          (ULONG)ViewSize);
        Mapping->Kind = DxgkpPhysicalMappingMdl;
        if (pArgs->AccessMode == DXGK_ACCESS_MODE_USER_MODE)
        {
            Mapping->Process = PsGetCurrentProcess();
            ObReferenceObject(Mapping->Process);
            _SEH2_TRY
            {
                Mapping->BaseAddress = MmMapLockedPagesSpecifyCache(
                                           Mapping->MappingMdl,
                                           UserMode,
                                           CacheType,
                                           NULL,
                                           FALSE,
                                           NormalPagePriority |
                                               MdlMappingNoExecute);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        else
        {
            Mapping->BaseAddress = MmMapLockedPagesSpecifyCache(
                                       Mapping->MappingMdl,
                                       KernelMode,
                                       CacheType,
                                       NULL,
                                       FALSE,
                                       NormalPagePriority);
        }
        if (!NT_SUCCESS(Status) || Mapping->BaseAddress == NULL)
        {
            if (Mapping->Process != NULL)
            {
                ObDereferenceObject(Mapping->Process);
                Mapping->Process = NULL;
            }
            IoFreeMdl(Mapping->MappingMdl);
            Mapping->MappingMdl = NULL;
            if (NT_SUCCESS(Status))
                Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }
    }

    InsertTailList(&Object->MappingList, &Mapping->ListEntry);
    pArgs->Offset = OffsetInView;
    pArgs->Size = ViewSize;
    pArgs->pMappedAddress = Mapping->BaseAddress;
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
    return STATUS_SUCCESS;

Failure:
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
    ExFreePoolWithTag(Mapping, TAG_DXGK_RESOURCES);
    return Status;
}

VOID
APIENTRY
DxgkCbUnmapPhysicalMemory(
    IN_CONST_PDXGKARGCB_UNMAP_PHYSICAL_MEMORY pArgs)
{
    PDXGKP_PHYSICAL_MEMORY_OBJECT Object;
    PLIST_ENTRY Link;
    PDXGKP_PHYSICAL_MAPPING Mapping = NULL;

    PAGED_CODE();

    if (pArgs == NULL || pArgs->hPhysicalMemoryObject == NULL ||
        pArgs->pBaseAddress == NULL || pArgs->Size == 0)
    {
        return;
    }

    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    Object = DxgkpFindPhysicalMemoryObjectLocked(
                 pArgs->hPhysicalMemoryObject);
    if (Object != NULL)
    {
        for (Link = Object->MappingList.Flink;
             Link != &Object->MappingList;
             Link = Link->Flink)
        {
            PDXGKP_PHYSICAL_MAPPING Candidate =
                CONTAINING_RECORD(Link,
                                  DXGKP_PHYSICAL_MAPPING,
                                  ListEntry);

            if (Candidate->BaseAddress == pArgs->pBaseAddress &&
                Candidate->Size == pArgs->Size &&
                (Candidate->Process == NULL ||
                 Candidate->Process == PsGetCurrentProcess()))
            {
                RemoveEntryList(Link);
                Mapping = Candidate;
                break;
            }
        }
    }

    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);

    if (Mapping != NULL)
        DxgkpFreePhysicalMapping(Mapping);
}

NTSTATUS
APIENTRY
DxgkCbAllocateAdl(
    IN_OUT_PDXGKARGCB_ALLOCATE_ADL pArgs)
{
    PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject;
    PDXGKP_PHYSICAL_MEMORY_OBJECT Object;
    PDXGKP_ADL_ENTRY Entry;
    PPFN_NUMBER MdlPages = NULL;
    DXGK_PAGE_NUMBER *Pages;
    DXGK_PAGE_NUMBER BasePage;
    ULONG PageCount;
    ULONG Index;
    BOOLEAN IsContiguous = TRUE;
    SIZE_T AllocationSize;

    PAGED_CODE();

    if (pArgs == NULL)
        return STATUS_INVALID_PARAMETER;
    pArgs->pAdl = NULL;

    if (pArgs->hAdapterMemoryObject == NULL ||
        pArgs->Size == 0 ||
        (pArgs->Offset & (PAGE_SIZE - 1)) != 0 ||
        (pArgs->Size & (PAGE_SIZE - 1)) != 0 ||
        (pArgs->Flags.Value & ~3U) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if ((pArgs->Size >> PAGE_SHIFT) > MAXULONG)
        return STATUS_INTEGER_OVERFLOW;

    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    AdapterObject = DxgkpFindAdapterMemoryObjectLocked(
                        pArgs->hAdapterMemoryObject);
    if (AdapterObject == NULL)
    {
        ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
        return STATUS_INVALID_HANDLE;
    }

    Object = AdapterObject->PhysicalObject;
    if (pArgs->Offset > Object->Size ||
        pArgs->Size > Object->Size - pArgs->Offset ||
        (pArgs->Flags.RequireContiguous &&
         Object->Type != DXGK_PHYSICAL_MEMORY_TYPE_CONTIGUOUS_MEMORY &&
         Object->Type != DXGK_PHYSICAL_MEMORY_TYPE_IO_SPACE))
    {
        ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
        return STATUS_INVALID_PARAMETER;
    }

    PageCount = (ULONG)(pArgs->Size >> PAGE_SHIFT);
    if (Object->Backing == DxgkpPhysicalBackingIoSpace)
    {
        BasePage = (DXGK_PAGE_NUMBER)
            ((Object->IoBaseAddress.QuadPart + pArgs->Offset) >> PAGE_SHIFT);
    }
    else
    {
        MdlPages = MmGetMdlPfnArray(Object->Mdl) +
                   (pArgs->Offset >> PAGE_SHIFT);
        BasePage = (DXGK_PAGE_NUMBER)MdlPages[0];
        for (Index = 1; Index < PageCount; Index++)
        {
            if ((DXGK_PAGE_NUMBER)MdlPages[Index] != BasePage + Index)
            {
                IsContiguous = FALSE;
                break;
            }
        }
    }

    AllocationSize = sizeof(*Entry);
    if (!(IsContiguous &&
          (pArgs->Flags.RequireContiguous ||
           pArgs->Flags.PreferContiguous)))
    {
        if (PageCount >
            (MAXULONG_PTR - AllocationSize) / sizeof(DXGK_PAGE_NUMBER))
        {
            ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
            return STATUS_INTEGER_OVERFLOW;
        }
        AllocationSize +=
            (SIZE_T)PageCount * sizeof(DXGK_PAGE_NUMBER);
    }

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  AllocationSize,
                                  TAG_DXGK_RESOURCES);
    if (Entry == NULL)
    {
        ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->AdapterMemoryObject = AdapterObject;
    Entry->Adl.PageCount = PageCount;
    if (IsContiguous &&
        (pArgs->Flags.RequireContiguous || pArgs->Flags.PreferContiguous))
    {
        Entry->Adl.Flags.Contiguous = 1;
        Entry->Adl.BasePageNumber = BasePage;
    }
    else
    {
        Pages = (DXGK_PAGE_NUMBER *)(Entry + 1);
        Entry->Adl.Pages = Pages;
        if (Object->Backing == DxgkpPhysicalBackingIoSpace)
        {
            for (Index = 0; Index < PageCount; Index++)
                Pages[Index] = BasePage + Index;
        }
        else
        {
            for (Index = 0; Index < PageCount; Index++)
                Pages[Index] = (DXGK_PAGE_NUMBER)MdlPages[Index];
        }
    }

    InsertTailList(&Object->AdlList, &Entry->ListEntry);
    pArgs->pAdl = &Entry->Adl;
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
    return STATUS_SUCCESS;
}

VOID
APIENTRY
DxgkCbFreeAdl(
    IN_CONST_PDXGKARGCB_FREE_ADL pArgs)
{
    PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject;
    PLIST_ENTRY Link;

    PAGED_CODE();

    if (pArgs == NULL || pArgs->hAdapterMemoryObject == NULL ||
        pArgs->pAdl == NULL)
    {
        return;
    }

    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    AdapterObject = DxgkpFindAdapterMemoryObjectLocked(
                        pArgs->hAdapterMemoryObject);
    if (AdapterObject != NULL)
    {
        for (Link = AdapterObject->PhysicalObject->AdlList.Flink;
             Link != &AdapterObject->PhysicalObject->AdlList;
             Link = Link->Flink)
        {
            PDXGKP_ADL_ENTRY Entry =
                CONTAINING_RECORD(Link, DXGKP_ADL_ENTRY, ListEntry);

            if (Entry->AdapterMemoryObject == AdapterObject &&
                &Entry->Adl == pArgs->pAdl)
            {
                RemoveEntryList(Link);
                ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
                break;
            }
        }
    }
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
}

NTSTATUS
APIENTRY
DxgkCbOpenPhysicalMemoryObject(
    IN_OUT_PDXGKARGCB_OPEN_PHYSICAL_MEMORY_OBJECT pArgs)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_PHYSICAL_MEMORY_OBJECT Object;
    PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pArgs == NULL)
        return STATUS_INVALID_PARAMETER;
    pArgs->hAdapterMemoryObject = NULL;

    if (pArgs->hPhysicalMemoryObject == NULL || pArgs->hAdapter == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(pArgs->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    AdapterObject = ExAllocatePoolWithTag(NonPagedPool,
                                          sizeof(*AdapterObject),
                                          TAG_DXGK_RESOURCES);
    if (AdapterObject == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    Object = DxgkpFindPhysicalMemoryObjectLocked(
                 pArgs->hPhysicalMemoryObject);
    if (Object == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else if (!IsListEmpty(&Object->AdapterMemoryList))
    {
        Status = STATUS_SHARING_VIOLATION;
    }
    else
    {
        AdapterObject->PhysicalObject = Object;
        AdapterObject->Adapter = Adapter;
        InsertTailList(&Object->AdapterMemoryList,
                       &AdapterObject->ListEntry);
        pArgs->hAdapterMemoryObject = (HANDLE)AdapterObject;
    }
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);

    if (!NT_SUCCESS(Status))
        ExFreePoolWithTag(AdapterObject, TAG_DXGK_RESOURCES);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

VOID
APIENTRY
DxgkCbClosePhysicalMemoryObject(
    IN_CONST_PDXGKARGCB_CLOSE_PHYSICAL_MEMORY_OBJECT pArgs)
{
    PDXGKP_ADAPTER_MEMORY_OBJECT AdapterObject = NULL;
    BOOLEAN OutstandingAdl = FALSE;

    PAGED_CODE();

    if (pArgs == NULL || pArgs->hAdapterMemoryObject == NULL)
        return;

    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    AdapterObject = DxgkpFindAdapterMemoryObjectLocked(
                        pArgs->hAdapterMemoryObject);
    if (AdapterObject != NULL)
    {
        if (DxgkpAdapterMemoryObjectHasAdlLocked(AdapterObject))
        {
            OutstandingAdl = TRUE;
            AdapterObject = NULL;
        }
        else
        {
            RemoveEntryList(&AdapterObject->ListEntry);
        }
    }
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);

    if (OutstandingAdl)
    {
        DXGKRNL_ERR("DxgkCbClosePhysicalMemoryObject: refusing to close adapter object %p with live ADLs\n", pArgs->hAdapterMemoryObject);
        return;
    }
    if (AdapterObject != NULL)
        ExFreePoolWithTag(AdapterObject, TAG_DXGK_RESOURCES);
}
#endif

static VOID
DxgkpFreeMapMemoryEntry(
    _In_ PDXGK_MAPMEM_ENTRY Entry)
{
    if (Entry->Kind == DxgkMapMemoryMdl)
    {
        KAPC_STATE ApcState;
        BOOLEAN Attached = FALSE;

        if (Entry->Process != NULL && Entry->Process != PsGetCurrentProcess())
        {
            KeStackAttachProcess((PKPROCESS)Entry->Process, &ApcState);
            Attached = TRUE;
        }
        MmUnmapLockedPages(Entry->BaseAddress, Entry->Mdl);
        if (Attached)
            KeUnstackDetachProcess(&ApcState);
        IoFreeMdl(Entry->Mdl);
    }
    else if (Entry->Kind == DxgkMapMemoryIoSpace)
    {
        MmUnmapIoSpace(Entry->BaseAddress, Entry->Length);
    }

    if (Entry->Process != NULL)
        ObDereferenceObject(Entry->Process);
    ExFreePoolWithTag(Entry, TAG_DXGK_RESOURCES);
}

static NTSTATUS
DxgkpBuildMapMemoryMdl(
    _In_ PHYSICAL_ADDRESS TranslatedAddress,
    _In_ ULONG Length,
    _Out_ PMDL *Mdl)
{
    PPFN_NUMBER Pages;
    PFN_NUMBER FirstPfn;
    PVOID OffsetAddress;
    ULONG PageCount;
    ULONG Index;
    PMDL NewMdl;

    *Mdl = NULL;
    OffsetAddress = (PVOID)(ULONG_PTR)BYTE_OFFSET(TranslatedAddress.LowPart);
    NewMdl = MmCreateMdl(NULL, OffsetAddress, Length);
    if (NewMdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    NewMdl->Process = NULL;
    NewMdl->MappedSystemVa = NULL;
    NewMdl->MdlFlags |= MDL_PAGES_LOCKED | MDL_IO_SPACE | MDL_MAPPING_CAN_FAIL;
    FirstPfn = (PFN_NUMBER)((ULONGLONG)TranslatedAddress.QuadPart >> PAGE_SHIFT);
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(OffsetAddress, Length);
    Pages = MmGetMdlPfnArray(NewMdl);
    for (Index = 0; Index < PageCount; ++Index)
        Pages[Index] = FirstPfn + Index;

    *Mdl = NewMdl;
    return STATUS_SUCCESS;
}

static VOID
DxgkpReleaseMapMemory(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY ReclaimList;
    PLIST_ENTRY Link;
    ULONG Reclaimed = 0;

    InitializeListHead(&ReclaimList);

    ExAcquireFastMutex(&DxgkpMapMemoryMutex);
    Link = DxgkpMapMemoryList.Flink;
    while (Link != &DxgkpMapMemoryList)
    {
        PLIST_ENTRY NextLink = Link->Flink;
        PDXGK_MAPMEM_ENTRY Entry =
            CONTAINING_RECORD(Link, DXGK_MAPMEM_ENTRY, ListEntry);

        if (Entry->Adapter == Adapter)
        {
            RemoveEntryList(Link);
            InsertTailList(&ReclaimList, Link);
        }
        Link = NextLink;
    }
    ExReleaseFastMutex(&DxgkpMapMemoryMutex);

    while (!IsListEmpty(&ReclaimList))
    {
        Link = RemoveHeadList(&ReclaimList);
        DxgkpFreeMapMemoryEntry(
            CONTAINING_RECORD(Link, DXGK_MAPMEM_ENTRY, ListEntry));
        Reclaimed++;
    }

    if (Reclaimed != 0)
    {
        DXGKRNL_WARN("DxgkpReleaseMapMemory: reclaimed %lu mapping(s) "
                     "left by adapter %p\n",
                     Reclaimed,
                     Adapter);
    }
}

/*
 * Remove process-owned reverse-callback mappings while the process address
 * space is still current and intact.  Adapter/object teardown remains the
 * fallback for kernel mappings and for abnormal miniport shutdown.
 */
VOID
DxgkAdapterProcessCleanup(
    _In_ PEPROCESS Process)
{
    LIST_ENTRY MapReclaimList;
    PLIST_ENTRY Link;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    LIST_ENTRY PhysicalReclaimList;
    PLIST_ENTRY ObjectLink;
#endif

    PAGED_CODE();

    if (Process == NULL)
        return;

    InitializeListHead(&MapReclaimList);
    ExAcquireFastMutex(&DxgkpMapMemoryMutex);
    Link = DxgkpMapMemoryList.Flink;
    while (Link != &DxgkpMapMemoryList)
    {
        PLIST_ENTRY NextLink = Link->Flink;
        PDXGK_MAPMEM_ENTRY Entry =
            CONTAINING_RECORD(Link, DXGK_MAPMEM_ENTRY, ListEntry);

        if (Entry->Process == Process)
        {
            RemoveEntryList(Link);
            InsertTailList(&MapReclaimList, Link);
        }
        Link = NextLink;
    }
    ExReleaseFastMutex(&DxgkpMapMemoryMutex);

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    InitializeListHead(&PhysicalReclaimList);
    ExAcquireFastMutex(&DxgkpPhysicalMemoryMutex);
    for (ObjectLink = DxgkpPhysicalMemoryList.Flink;
         ObjectLink != &DxgkpPhysicalMemoryList;
         ObjectLink = ObjectLink->Flink)
    {
        PDXGKP_PHYSICAL_MEMORY_OBJECT Object =
            CONTAINING_RECORD(ObjectLink,
                              DXGKP_PHYSICAL_MEMORY_OBJECT,
                              ListEntry);

        Link = Object->MappingList.Flink;
        while (Link != &Object->MappingList)
        {
            PLIST_ENTRY NextLink = Link->Flink;
            PDXGKP_PHYSICAL_MAPPING Mapping =
                CONTAINING_RECORD(Link,
                                  DXGKP_PHYSICAL_MAPPING,
                                  ListEntry);

            if (Mapping->Process == Process)
            {
                RemoveEntryList(Link);
                InsertTailList(&PhysicalReclaimList, Link);
            }
            Link = NextLink;
        }
    }
    ExReleaseFastMutex(&DxgkpPhysicalMemoryMutex);
#endif

    while (!IsListEmpty(&MapReclaimList))
    {
        Link = RemoveHeadList(&MapReclaimList);
        DxgkpFreeMapMemoryEntry(
            CONTAINING_RECORD(Link, DXGK_MAPMEM_ENTRY, ListEntry));
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    while (!IsListEmpty(&PhysicalReclaimList))
    {
        Link = RemoveHeadList(&PhysicalReclaimList);
        DxgkpFreePhysicalMapping(
            CONTAINING_RECORD(Link,
                              DXGKP_PHYSICAL_MAPPING,
                              ListEntry));
    }
#endif
}

/*
 * DxgkCbMapMemory
 *
 * Baseline callback retained by the WDDM 2.x/3.x interface — maps a range of
 * translated physical addresses into kernel virtual address space or the
 * current user process.
 *
 * Device-memory resources are mapped with MmMapIoSpace. User mappings use an
 * I/O-space MDL whose PFNs cover only the adapter-assigned translated range.
 * Every successful mapping is tracked by adapter and, for a user view, by
 * process so UnmapMemory and teardown cannot release another owner's view.
 *
 * Unlike DxgkCbMapPhysicalMemory (WDDM 2.9), this callback takes
 * individual parameters rather than a structure pointer.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbMapMemory(
    _In_  HANDLE              DeviceHandle,
    _In_  PHYSICAL_ADDRESS    TranslatedAddress,
    _In_  ULONG               Length,
    _In_  BOOLEAN             InIoSpace,
    _In_  BOOLEAN             MapToUserMode,
    _In_  MEMORY_CACHING_TYPE CacheType,
    _Out_ PVOID              *VirtualAddress)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGK_MAPMEM_ENTRY MapEntry;
    PVOID Va = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG TotalStart100ns;
    ULONGLONG MapStart100ns;
    ULONGLONG MapUs = 0;
    PCSTR MapMethod = "unmapped";

    PAGED_CODE();
    TotalStart100ns = DxgkpTraceNow100ns();

    if (VirtualAddress == NULL)
        return STATUS_INVALID_PARAMETER;
    *VirtualAddress = NULL;

    if (Length == 0)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    if (CacheType < MmNonCached || CacheType > MmWriteCombined || !DxgkAdapterMapRangeAssigned(Adapter->TranslatedResources, TranslatedAddress, Length, InIoSpace))
    {
        /* A refused mapping is how a miniport loses an aperture it needs, and
         * the caller only sees STATUS_INVALID_PARAMETER. */
        DXGKRNL_ERR("DxgkCbMapMemory: refused adapter %p PA=0x%I64X len=0x%lX "
                    "io=%d cache=%d (not in the adapter's translated resources)\n",
                    Adapter, TranslatedAddress.QuadPart, Length, InIoSpace, CacheType);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_PARAMETER;
    }

    DXGKRNL_TRACE("DxgkCbMapMemory: enter PA=0x%I64X Len=0x%lX IoSpace=%d UserMode=%d Cache=%d\n",
                  TranslatedAddress.QuadPart, Length, InIoSpace, MapToUserMode, CacheType);

    MapEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*MapEntry), TAG_DXGK_RESOURCES);
    if (MapEntry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(MapEntry, sizeof(*MapEntry));
    MapEntry->Adapter = Adapter;
    MapEntry->PhysicalAddress = TranslatedAddress;
    MapEntry->Length = Length;

    MapStart100ns = DxgkpTraceNow100ns();
    if (InIoSpace)
    {
        /*
         * MapToUserMode is ignored for I/O-space ranges by the public DDI.
         * x86 port accessors consume the translated port address directly;
         * architectures with memory-backed port access need an MMIO mapping.
         */
#if defined(_M_AMD64) || defined(_M_IX86)
        Va = (PVOID)(ULONG_PTR)TranslatedAddress.QuadPart;
        MapMethod = "port-space";
        MapEntry->Kind = DxgkMapMemoryPortSpace;
#else
        Va = MmMapIoSpace(TranslatedAddress, Length, MmNonCached);
        MapMethod = "iospace-port";
        MapEntry->Kind = DxgkMapMemoryIoSpace;
#endif
    }
    else if (MapToUserMode)
    {
        Status = DxgkpBuildMapMemoryMdl(TranslatedAddress, Length, &MapEntry->Mdl);
        if (NT_SUCCESS(Status))
        {
            _SEH2_TRY
            {
                Va = MmMapLockedPagesSpecifyCache(MapEntry->Mdl, UserMode, CacheType, NULL, FALSE, NormalPagePriority | MdlMappingNoExecute);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        if (NT_SUCCESS(Status) && Va == NULL)
            Status = STATUS_INSUFFICIENT_RESOURCES;
        if (NT_SUCCESS(Status))
        {
            MapEntry->Process = PsGetCurrentProcess();
            ObReferenceObject(MapEntry->Process);
            MapEntry->Kind = DxgkMapMemoryMdl;
            MapMethod = "user-mdl";
        }
    }
    else
    {
        Va = MmMapIoSpace(TranslatedAddress, Length, CacheType);
        MapMethod = "mmmapiospace";
        MapEntry->Kind = DxgkMapMemoryIoSpace;
    }
    MapUs = DxgkpTraceElapsedUs(MapStart100ns);

    if (!NT_SUCCESS(Status) || Va == NULL)
    {
        if (MapEntry->Mdl != NULL)
            IoFreeMdl(MapEntry->Mdl);
        ExFreePoolWithTag(MapEntry, TAG_DXGK_RESOURCES);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return NT_SUCCESS(Status) ? STATUS_INSUFFICIENT_RESOURCES : Status;
    }

    MapEntry->VirtualAddress = Va;
    MapEntry->BaseAddress = Va;
    MapEntry->MapMethod = MapMethod;
    ExAcquireFastMutex(&DxgkpMapMemoryMutex);
    InsertTailList(&DxgkpMapMemoryList, &MapEntry->ListEntry);
    ExReleaseFastMutex(&DxgkpMapMemoryMutex);
    *VirtualAddress = Va;

    DXGKRNL_TRACE("DxgkCbMapMemory: PA=0x%I64X -> VA=%p Len=0x%lX IoSpace=%d UserMode=%d Cache=%d via=%s map=%I64u us total=%I64u us\n",
                  TranslatedAddress.QuadPart, Va, Length, InIoSpace, MapToUserMode, CacheType,
                  MapMethod, MapUs, DxgkpTraceElapsedUs(TotalStart100ns));

    /*
     * A miniport that fails DxgkDdiStartDevice leaves nothing behind but the
     * mappings it took.  Name the first few so a start that gives up part way
     * through says which aperture it had reached.
     */
    {
        LONG MapCount = InterlockedIncrement(&Adapter->MapMemoryCallCount);

        if (MapCount <= 8)
        {
            DXGKRNL_INFO("DxgkCbMapMemory #%ld: adapter %p PA=0x%I64X len=0x%lX "
                        "io=%d user=%d cache=%d -> %p (%s)\n",
                        MapCount, Adapter, TranslatedAddress.QuadPart, Length,
                        InIoSpace, MapToUserMode, CacheType, Va, MapMethod);
        }
    }

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

/*
 * DxgkCbUnmapMemory (baseline WDDM callback)
 *
 * Unmaps an address range previously mapped by DxgkCbMapMemory.
 * Takes a HANDLE + PVOID (simpler than the WDDM 2.9 struct-based variant).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbUnmapMemory(
    _In_ HANDLE DeviceHandle,
    _In_ PVOID  VirtualAddress)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGK_MAPMEM_ENTRY MapEntry = NULL;
    PLIST_ENTRY Link;

    PAGED_CODE();

    if (VirtualAddress == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    ExAcquireFastMutex(&DxgkpMapMemoryMutex);
    for (Link = DxgkpMapMemoryList.Flink;
         Link != &DxgkpMapMemoryList;
         Link = Link->Flink)
    {
        PDXGK_MAPMEM_ENTRY Candidate =
            CONTAINING_RECORD(Link, DXGK_MAPMEM_ENTRY, ListEntry);

        if (Candidate->VirtualAddress == VirtualAddress && DxgkAdapterMapOwnerMatches(Candidate->Adapter, Candidate->Process, Adapter, PsGetCurrentProcess()))
        {
            MapEntry = Candidate;
            RemoveEntryList(&MapEntry->ListEntry);
            break;
        }
    }
    ExReleaseFastMutex(&DxgkpMapMemoryMutex);

    if (MapEntry == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_INVALID_PARAMETER;
    }

    DxgkpFreeMapMemoryEntry(MapEntry);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

/*
 * DxgkCbQueueDpc
 *
 * Called from the miniport's ISR (at any IRQL) to queue a DPC that will
 * invoke DxgkDdiDpcRoutine at DISPATCH_LEVEL.  Returns TRUE if the DPC
 * was queued, FALSE if one was already pending.
 *
 * IRQL: Any level (called from ISR)
 */
BOOLEAN
APIENTRY
DxgkCbQueueDpc(
    _In_ HANDLE DeviceHandle)
{
    PDXGKRNL_ADAPTER Adapter;
    BOOLEAN          Queued;
    LONG             Sequence;

    Adapter = (PDXGKRNL_ADAPTER)DeviceHandle;
    if (Adapter == NULL)
        return FALSE;
    if (!DxgkpAcquireVidSchCallback(Adapter))
        return FALSE;

    Sequence = InterlockedIncrement(&Adapter->QueueDpcCount);
    Queued = KeInsertQueueDpc(&Adapter->DpcObject, NULL, NULL);
    if (Sequence <= DXGK_TRACE_DPC_LOG_LIMIT)
    {
        DXGKRNL_TRACE("DxgkCbQueueDpc: seq=%ld queued=%d state=%d irq=%ld dpc=%ld t+%I64u us\n",
                      Sequence,
                      Queued,
                      Adapter->State,
                      Adapter->InterruptCount,
                      Adapter->DpcCount,
                      DxgkpTraceSinceStartUs(Adapter));
    }

    DxgkpReleaseVidSchCallback(Adapter);
    return Queued;
}

/*
 * DxgkCbReadDeviceSpace
 *
 * Reads device space owned by the display adapter.
 *
 * DataType values follow the Windows display-miniport ABI:
 *   DXGK_WHICHSPACE_CONFIG (PCI_WHICHSPACE_CONFIG) — adapter config space
 *   DXGK_WHICHSPACE_ROM    (PCI_WHICHSPACE_ROM)    — expansion ROM
 *   DXGK_WHICHSPACE_MCH    (0x80000000)            — host bridge/MCH
 *   DXGK_WHICHSPACE_BRIDGE (0x80000001)            — upstream PCI bridge
 *
 * IRQL: PASSIVE_LEVEL
 */
C_ASSERT(DXGK_WHICHSPACE_CONFIG == PCI_WHICHSPACE_CONFIG);
C_ASSERT(DXGK_WHICHSPACE_ROM == PCI_WHICHSPACE_ROM);
C_ASSERT(DXGK_WHICHSPACE_MCH == 0x80000000);
C_ASSERT(DXGK_WHICHSPACE_BRIDGE == 0x80000001);

NTSTATUS
APIENTRY
DxgkCbReadDeviceSpace(
    _In_  HANDLE  DeviceHandle,
    _In_  ULONG   DataType,
    _In_  PVOID   Buffer,
    _In_  ULONG   Offset,
    _In_  ULONG   Length,
    _Out_ PULONG  BytesRead)
{
    PDXGKRNL_ADAPTER Adapter;
    ULONG            BytesTransferred;
    ULONGLONG        TotalStart100ns;
    ULONGLONG        ElapsedUs;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    if (BytesRead == NULL)
        return STATUS_INVALID_PARAMETER;

    *BytesRead = 0;

    if (Buffer == NULL || Length == 0)
        return STATUS_INVALID_PARAMETER;

    /* Resolve and pin the adapter while it remains on the global list. */
    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbReadDeviceSpace: invalid handle %p\n", DeviceHandle);
        return STATUS_INVALID_HANDLE;
    }

    if (DataType == DXGK_WHICHSPACE_CONFIG)
    {
        if (!Adapter->PciBusInterfaceValid ||
            Adapter->PciBusInterface.GetBusData == NULL)
        {
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
            return STATUS_UNSUCCESSFUL;
        }

        BytesTransferred = Adapter->PciBusInterface.GetBusData(
            Adapter->PciBusInterface.Context,
            PCI_WHICHSPACE_CONFIG,
            Buffer,
            Offset,
            Length);

        if (BytesTransferred == 0 || BytesTransferred > Length)
        {
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
            return STATUS_UNSUCCESSFUL;
        }

        *BytesRead = BytesTransferred;
        ElapsedUs = DxgkpTraceElapsedUs(TotalStart100ns);
        if (ElapsedUs >= DXGK_TRACE_SLOW_CONFIG_ACCESS_US)
        {
            DXGKRNL_WARN("DxgkCbReadDeviceSpace: slow config read Off=0x%lX Len=0x%lX Bytes=%lu took %I64u us\n",
                         Offset, Length, BytesTransferred, ElapsedUs);
        }
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_SUCCESS;
    }

    if (DataType == DXGK_WHICHSPACE_ROM ||
        DataType == DXGK_WHICHSPACE_MCH ||
        DataType == DXGK_WHICHSPACE_BRIDGE)
    {
        DXGKRNL_WARN("DxgkCbReadDeviceSpace: unavailable DataType 0x%08lX\n",
                     DataType);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_UNSUCCESSFUL;
    }
    else
        DXGKRNL_WARN("DxgkCbReadDeviceSpace: unsupported DataType 0x%08lX\n", DataType);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_INVALID_PARAMETER;
}

/*
 * DxgkCbWriteDeviceSpace
 *
 * Writes to the PCI configuration space of the display adapter.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbWriteDeviceSpace(
    _In_  HANDLE  DeviceHandle,
    _In_  ULONG   DataType,
    _In_  PVOID   Buffer,
    _In_  ULONG   Offset,
    _In_  ULONG   Length,
    _Out_ PULONG  BytesWritten)
{
    PDXGKRNL_ADAPTER Adapter;
    ULONG            BytesTransferred;
    ULONGLONG        TotalStart100ns;
    ULONGLONG        ElapsedUs;

    PAGED_CODE();

    TotalStart100ns = DxgkpTraceNow100ns();

    if (BytesWritten == NULL)
        return STATUS_INVALID_PARAMETER;

    *BytesWritten = 0;

    if (Buffer == NULL || Length == 0)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbWriteDeviceSpace: invalid handle %p\n", DeviceHandle);
        return STATUS_INVALID_HANDLE;
    }

    /* No per-write trace to avoid stack overflow from serial output. */

    if (DataType == DXGK_WHICHSPACE_CONFIG)
    {
        if (!Adapter->PciBusInterfaceValid ||
            Adapter->PciBusInterface.SetBusData == NULL)
        {
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
            return STATUS_UNSUCCESSFUL;
        }

        BytesTransferred = Adapter->PciBusInterface.SetBusData(
            Adapter->PciBusInterface.Context,
            PCI_WHICHSPACE_CONFIG,
            Buffer,
            Offset,
            Length);

        if (BytesTransferred == 0 || BytesTransferred > Length)
        {
            ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
            return STATUS_UNSUCCESSFUL;
        }

        *BytesWritten = BytesTransferred;
        ElapsedUs = DxgkpTraceElapsedUs(TotalStart100ns);
        if (ElapsedUs >= DXGK_TRACE_SLOW_CONFIG_ACCESS_US)
        {
            DXGKRNL_WARN("DxgkCbWriteDeviceSpace: slow config write Off=0x%lX Len=0x%lX Bytes=%lu took %I64u us\n",
                         Offset, Length, BytesTransferred, ElapsedUs);
        }
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_SUCCESS;
    }

    if (DataType == DXGK_WHICHSPACE_ROM ||
        DataType == DXGK_WHICHSPACE_MCH ||
        DataType == DXGK_WHICHSPACE_BRIDGE)
    {
        DXGKRNL_WARN("DxgkCbWriteDeviceSpace: unavailable DataType 0x%08lX\n",
                     DataType);
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_UNSUCCESSFUL;
    }
    else
        DXGKRNL_WARN("DxgkCbWriteDeviceSpace: unsupported DataType 0x%08lX\n", DataType);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_INVALID_PARAMETER;
}

/*
 * DxgkCbIndicateChildStatus
 *
 * Called by the miniport when a child device's connection status changes
 * (e.g. a monitor is hot-plugged or removed).  Invalidates the bus
 * relations for the FDO, causing the PnP manager to re-enumerate children.
 * Also triggers a VidPN rebuild to update the display topology.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbIndicateChildStatus(
    _In_ HANDLE              DeviceHandle,
    _In_ PDXGK_CHILD_STATUS  ChildStatus)
{
    PDXGKRNL_ADAPTER Adapter;
    BOOLEAN Changed;
    NTSTATUS Status = STATUS_SUCCESS;

    if (ChildStatus == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ChildStatus->Type != StatusConnection)
    {
        if (ChildStatus->Type == StatusRotation ||
            ChildStatus->Type == StatusMiracastConnection)
        {
            return STATUS_NOT_SUPPORTED;
        }
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbIndicateChildStatus: invalid handle %p\n",
                    DeviceHandle);
        return STATUS_INVALID_HANDLE;
    }

    DXGKRNL_TRACE("DxgkCbIndicateChildStatus: adapter %p type %d uid %lu "
                  "connected=%d\n",
                  Adapter,
                  ChildStatus ? ChildStatus->Type : -1,
                  ChildStatus ? ChildStatus->ChildUid : 0,
                  (ChildStatus && ChildStatus->Type == StatusConnection)
                      ? ChildStatus->HotPlug.Connected : -1);

    /* Publish the connector state before any deferred topology snapshot.  The
     * rebuild is never run inside a reverse callback because the miniport may
     * already own the adapter KMD transaction on this thread. */
    Status = DxgkPnpIndicateChildConnection(
                 Adapter,
                 ChildStatus->ChildUid,
                 ChildStatus->HotPlug.Connected,
                 &Changed);
    DXGKRNL_INFO("CHILD_INDICATION: adapter=%p uid=%lu connected=%u "
                 "state=%u changed=%u status=0x%08lX\n",
                 Adapter, ChildStatus->ChildUid,
                 (UINT)ChildStatus->HotPlug.Connected,
                 (UINT)Adapter->State, (UINT)Changed, Status);
    if (NT_SUCCESS(Status) && Changed)
    {
        Status = DxgkVidPnQueueHotPlugRebuild(Adapter);
        IoInvalidateDeviceRelations(Adapter->PhysicalDeviceObject,
                                    BusRelations);
    }

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

/*
 * DxgkCbQueryServices
 *
 * Returns only service interfaces backed by an actual dxgkrnl provider.
 * Debug-report, AGP, and the former ReactOS selector-1 bus extension remain
 * unavailable rather than publishing callback tables with no consumer.
 */
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
/*
 * System Firmware Table interface (DxgkServicesFirmwareTable)
 *
 * A display miniport reads firmware description tables to find data the
 * firmware left for it: the AMD miniport, for one, reads the ACPI VFCT table
 * to recover the adapter's video BIOS image.  The tables live in the ACPI
 * driver, which publishes them on GUID_ACPI_SYSTEM_INTERFACE, so both entry
 * points below are thin forwarders to that device.
 *
 * A miniport that asks for this interface and gets a failure may still call
 * through the structure it passed in, so every published entry has to be a
 * real routine; leaving one null turns an unsupported provider into a jump to
 * address zero.
 */

/* 'ACPI', as the provider signature is spelled on the wire */
#define DXGKP_FIRMWARE_PROVIDER_ACPI 0x41435049

#define DXGKP_FIRMWARE_TABLE_LIMIT (16 * 1024 * 1024)

static VOID
NTAPI
DxgkpFirmwareTableInterfaceReferenceNop(
    _In_opt_ PVOID Context)
{
    /* Same-stack interface: dxgkrnl outlives the miniport that holds it. */
    UNREFERENCED_PARAMETER(Context);
}

static NTSTATUS
DxgkpFirmwareTableSendIoctl(
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputSize) PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer,
    _In_ ULONG OutputSize,
    _Out_ PULONG_PTR Information)
{
    PWSTR InterfaceList = NULL;
    PFILE_OBJECT FileObject = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    UNICODE_STRING InterfaceName;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    *Information = 0;

    /*
     * Opening the ACPI device interface takes the passive level.  A miniport
     * that asks for a firmware table from a raised IRQL gets a clean refusal
     * rather than an assertion, because the table contents never change and
     * the caller can read them during start-up instead.
     */
    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    Status = IoGetDeviceInterfaces(&GUID_ACPI_SYSTEM_INTERFACE,
                                   NULL,
                                   0,
                                   &InterfaceList);
    if (!NT_SUCCESS(Status))
        return Status;

    if (InterfaceList == NULL || InterfaceList[0] == UNICODE_NULL)
    {
        if (InterfaceList != NULL)
            ExFreePool(InterfaceList);
        return STATUS_NOT_FOUND;
    }

    RtlInitUnicodeString(&InterfaceName, InterfaceList);
    Status = IoGetDeviceObjectPointer(&InterfaceName,
                                      FILE_READ_DATA | SYNCHRONIZE,
                                      &FileObject,
                                      &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(InterfaceList);
        return Status;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    RtlZeroMemory(&IoStatus, sizeof(IoStatus));

    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceObject,
                                        InputBuffer,
                                        InputSize,
                                        OutputBuffer,
                                        OutputSize,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
    {
        ObDereferenceObject(FileObject);
        ExFreePool(InterfaceList);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    *Information = IoStatus.Information;

    ObDereferenceObject(FileObject);
    ExFreePool(InterfaceList);
    return Status;
}

static NTSTATUS
DxgkpFirmwareTableEnumTables(
    _In_ PVOID Context,
    _In_ ULONG ProviderSignature,
    _In_ ULONG BufferSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _Out_ PULONG RequiredSize)
{
    PACPI_ENUM_SYSTEM_TABLES_ENTRY Entries = NULL;
    ULONG_PTR Information = 0;
    ULONG EntrySize = sizeof(*Entries);
    ULONG Capacity = 64;
    ULONG Count;
    ULONG Index;
    NTSTATUS Status;


    UNREFERENCED_PARAMETER(Context);

    if (RequiredSize == NULL)
        return STATUS_INVALID_PARAMETER;

    *RequiredSize = 0;

    if (Buffer == NULL && BufferSize != 0)
        return STATUS_INVALID_PARAMETER;

    /* Only the ACPI provider is described by the ACPI driver. */
    if (ProviderSignature != DXGKP_FIRMWARE_PROVIDER_ACPI)
        return STATUS_NOT_SUPPORTED;

    for (;;)
    {
        Entries = ExAllocatePoolWithTag(PagedPool,
                                        Capacity * EntrySize,
                                        TAG_DXGK_RESOURCES);
        if (Entries == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = DxgkpFirmwareTableSendIoctl(IOCTL_ACPI_ENUM_SYSTEM_TABLES,
                                             NULL,
                                             0,
                                             Entries,
                                             Capacity * EntrySize,
                                             &Information);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;

        ExFreePoolWithTag(Entries, TAG_DXGK_RESOURCES);
        Entries = NULL;

        if (Information <= (ULONG_PTR)(Capacity * EntrySize) ||
            Information > DXGKP_FIRMWARE_TABLE_LIMIT)
        {
            return STATUS_ACPI_INVALID_DATA;
        }
        Capacity = (ULONG)(Information / EntrySize);
    }

    if (!NT_SUCCESS(Status))
    {
        if (Entries != NULL)
            ExFreePoolWithTag(Entries, TAG_DXGK_RESOURCES);
        return Status;
    }

    Count = (ULONG)(Information / EntrySize);

    /* The caller wants the table identifiers, which for ACPI are signatures. */
    *RequiredSize = Count * sizeof(ULONG);
    if (BufferSize < *RequiredSize)
    {
        ExFreePoolWithTag(Entries, TAG_DXGK_RESOURCES);
        return STATUS_BUFFER_TOO_SMALL;
    }

    for (Index = 0; Index < Count; Index++)
    {
        ULONG TableId;

        RtlCopyMemory(&TableId, Entries[Index].Signature, sizeof(TableId));
        ((PULONG)Buffer)[Index] = TableId;
    }

    ExFreePoolWithTag(Entries, TAG_DXGK_RESOURCES);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpFirmwareTableReadTable(
    _In_ PVOID Context,
    _In_ ULONG ProviderSignature,
    _In_ ULONG TableId,
    _In_ ULONG BufferSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _Out_ PULONG RequiredSize)
{
    ACPI_GET_SYSTEM_TABLE_INPUT Input;
    ULONG_PTR Information = 0;
    NTSTATUS Status;


    UNREFERENCED_PARAMETER(Context);

    if (RequiredSize == NULL)
        return STATUS_INVALID_PARAMETER;

    *RequiredSize = 0;

    if (Buffer == NULL && BufferSize != 0)
        return STATUS_INVALID_PARAMETER;

    if (ProviderSignature != DXGKP_FIRMWARE_PROVIDER_ACPI)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&Input, sizeof(Input));
    RtlCopyMemory(Input.Signature, &TableId, sizeof(Input.Signature));
    Input.Instance = 1;

    Status = DxgkpFirmwareTableSendIoctl(IOCTL_ACPI_GET_SYSTEM_TABLE,
                                         &Input,
                                         sizeof(Input),
                                         Buffer,
                                         BufferSize,
                                         &Information);

    DXGKRNL_INFO("firmware table '%.4s' provider '%.4s' -> 0x%08lx, %lu byte(s)\n",
                (PCHAR)&TableId, (PCHAR)&ProviderSignature,
                Status, (ULONG)Information);

    /*
     * The size query is the ordinary first call: the caller passes no buffer
     * and reads the length out of RequiredSize, so the length has to survive
     * the buffer-too-small answer.
     */
    if (Information > DXGKP_FIRMWARE_TABLE_LIMIT)
        return STATUS_ACPI_INVALID_DATA;

    if (Status == STATUS_BUFFER_TOO_SMALL || NT_SUCCESS(Status))
        *RequiredSize = (ULONG)Information;

    return Status;
}
#endif /* REACTOS_WDDM_TARGET_LEVEL >= 3200 */

#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
static VOID
NTAPI
DxgkpFeatureInterfaceReferenceNop(
    _In_opt_ PVOID Context)
{
    /*
     * This is a same-stack interface: dxgkrnl cannot unload while its
     * display miniport is running. Each callback validates the opaque adapter
     * handle and holds ReverseCallbackRundownRef for the duration of the call.
     */
    UNREFERENCED_PARAMETER(Context);
}

static NTSTATUS
APIENTRY
DxgkpFeatureIsEnabled(
    _In_ HANDLE DeviceHandle,
    INOUT_PDXGKARGCB_ISFEATUREENABLED2 Args)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    if (Args == NULL)
        return STATUS_INVALID_PARAMETER;

    Args->Result.Version = 0;
    Args->Result.Value = 0;
    if (Args->Flags.Value != 0)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Status = DxgkQueryFeatureState(Adapter,
                                   Args->FeatureId,
                                   &Args->Result);
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

static NTSTATUS
APIENTRY
DxgkpFeatureQueryInterface(
    _In_ HANDLE DeviceHandle,
    INOUT_PDXGKARGCB_QUERYFEATUREINTERFACE Args)
{
    PDXGKRNL_ADAPTER Adapter;

    if (Args == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);

    /*
     * None of the OS-side features ReactOS currently enables has a
     * feature-specific callback table. In particular, KMD-signaled CPU
     * events use the main DXGK_INTERFACE callback.
     */
    return STATUS_NOT_SUPPORTED;
}
#endif

/*
 * DxgkServicesTimedOperation
 *
 * A miniport wraps its hardware polls during start, reset and power
 * transitions in a timed operation so the OS can bound them.  The deadline
 * runs from TimedOperationStart on the interrupt-time clock and is capped at
 * DXGK_TIMED_OPERATION_TIMEOUT_MAX_SECONDS.  Windows escalates an OsHandled
 * expiry into a TDR; ReactOS reports it once, marks the operation and
 * returns STATUS_TIMEOUT so the miniport's own fallback runs.
 */
static VOID
NTAPI
DxgkpServicesInterfaceReferenceNop(
    _In_opt_ PVOID Context)
{
    /* Same-stack interface: dxgkrnl outlives the miniport that holds it. */
    UNREFERENCED_PARAMETER(Context);
}

static NTSTATUS
DxgkpTimedOperationStart(
    _Inout_ DXGK_TIMED_OPERATION *Op,
    _In_ const LARGE_INTEGER *Timeout,
    _In_ BOOLEAN OsHandled)
{
    LONGLONG Limit;

    if (Op == NULL || Timeout == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Relative NT intervals are negative; a positive value is taken as the
     * same magnitude rather than as an absolute time. */
    Limit = Timeout->QuadPart < 0 ? -Timeout->QuadPart : Timeout->QuadPart;
    if (Limit > DXGK_TIMED_OPERATION_TIMEOUT_MAX_SECONDS * 10000000LL)
        Limit = DXGK_TIMED_OPERATION_TIMEOUT_MAX_SECONDS * 10000000LL;

    RtlZeroMemory(Op, sizeof(*Op));
    Op->Size = sizeof(*Op);
    Op->OwnerTag = (ULONG_PTR)PsGetCurrentThread();
    Op->OsHandled = OsHandled;
    Op->Timeout.QuadPart = Limit;
    Op->StartTick.QuadPart = (LONGLONG)KeQueryInterruptTime();
    return STATUS_SUCCESS;
}

/* Time left before the deadline, in 100 ns units; zero once it has passed. */
static LONGLONG
DxgkpTimedOperationRemaining(
    _In_ const DXGK_TIMED_OPERATION *Op)
{
    LONGLONG Elapsed;

    Elapsed = (LONGLONG)KeQueryInterruptTime() - Op->StartTick.QuadPart;
    if (Elapsed < 0)
        Elapsed = 0;
    return Elapsed < Op->Timeout.QuadPart ? Op->Timeout.QuadPart - Elapsed : 0;
}

static NTSTATUS
DxgkpTimedOperationExpire(
    _Inout_ DXGK_TIMED_OPERATION *Op)
{
    if (!Op->TimeoutTriggered)
    {
        Op->TimeoutTriggered = TRUE;
        DXGKRNL_ERR("DxgkServicesTimedOperation: thread %p exceeded its "
                    "%I64u ms deadline (OsHandled=%u)\n",
                    (PVOID)Op->OwnerTag,
                    Op->Timeout.QuadPart / 10000,
                    Op->OsHandled);
    }
    return STATUS_TIMEOUT;
}

static NTSTATUS
DxgkpTimedOperationDelay(
    _Inout_ DXGK_TIMED_OPERATION *Op,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_ const LARGE_INTEGER *Interval)
{
    LARGE_INTEGER Bounded;
    LONGLONG Remaining;
    NTSTATUS Status;

    if (Op == NULL || Interval == NULL)
        return STATUS_INVALID_PARAMETER;

    Remaining = DxgkpTimedOperationRemaining(Op);
    if (Remaining == 0)
        return DxgkpTimedOperationExpire(Op);

    /* Never sleep past the deadline, whatever interval the caller picked. */
    Bounded = *Interval;
    if (Bounded.QuadPart < 0 && -Bounded.QuadPart > Remaining)
        Bounded.QuadPart = -Remaining;
    Status = KeDelayExecutionThread(WaitMode, Alertable, &Bounded);
    if (NT_SUCCESS(Status) && DxgkpTimedOperationRemaining(Op) == 0)
        return DxgkpTimedOperationExpire(Op);
    return Status;
}

static NTSTATUS
DxgkpTimedOperationWaitForSingleObject(
    _Inout_ DXGK_TIMED_OPERATION *Op,
    _In_ PVOID Object,
    _In_ KWAIT_REASON WaitReason,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ const LARGE_INTEGER *Timeout)
{
    LARGE_INTEGER Bounded;
    LARGE_INTEGER Now;
    LONGLONG Requested;
    LONGLONG Remaining;
    NTSTATUS Status;

    if (Op == NULL || Object == NULL)
        return STATUS_INVALID_PARAMETER;

    Remaining = DxgkpTimedOperationRemaining(Op);
    if (Remaining == 0)
        return DxgkpTimedOperationExpire(Op);

    /* The caller's own timeout still applies when it is the shorter one; an
     * absolute time is measured against the system clock first. */
    Requested = Remaining;
    if (Timeout != NULL)
    {
        if (Timeout->QuadPart <= 0)
        {
            Requested = -Timeout->QuadPart;
        }
        else
        {
            KeQuerySystemTime(&Now);
            Requested = Timeout->QuadPart > Now.QuadPart ?
                        Timeout->QuadPart - Now.QuadPart : 0;
        }
    }
    Bounded.QuadPart = -min(Requested, Remaining);
    Status = KeWaitForSingleObject(Object,
                                   WaitReason,
                                   WaitMode,
                                   Alertable,
                                   &Bounded);
    if (Status == STATUS_TIMEOUT && DxgkpTimedOperationRemaining(Op) == 0)
        return DxgkpTimedOperationExpire(Op);
    return Status;
}

/*
 * DxgkServicesDebugReport
 *
 * A miniport files a report when it decides hardware or firmware misbehaved.
 * Windows spools it for Online Crash Analysis; ReactOS has no consumer, so
 * the code and arguments go to the debug log -- where a failed start is
 * diagnosed anyway -- and the secondary data is only counted.
 */
typedef struct _DXGKP_DEBUG_REPORT
{
    PDXGKRNL_ADAPTER Adapter;
    ULONG Code;
    ULONG_PTR Arguments[4];
    ULONG SecondaryDataBytes;
} DXGKP_DEBUG_REPORT, *PDXGKP_DEBUG_REPORT;

static DXGK_DEBUG_REPORT_HANDLE
DxgkpDbgReportCreate(
    _In_ HANDLE DeviceHandle,
    _In_ ULONG Code,
    _In_ ULONG_PTR Arg1,
    _In_ ULONG_PTR Arg2,
    _In_ ULONG_PTR Arg3,
    _In_ ULONG_PTR Arg4)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_DEBUG_REPORT Report;

    PAGED_CODE();

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return NULL;

    DXGKRNL_ERR("DbgReport: adapter %p code=0x%08lx args=0x%Ix 0x%Ix 0x%Ix 0x%Ix\n",
                Adapter, Code, Arg1, Arg2, Arg3, Arg4);

    Report = ExAllocatePoolWithTag(PagedPool, sizeof(*Report), TAG_DXGK_ADAPTER);
    if (Report != NULL)
    {
        Report->Adapter = Adapter;
        Report->Code = Code;
        Report->Arguments[0] = Arg1;
        Report->Arguments[1] = Arg2;
        Report->Arguments[2] = Arg3;
        Report->Arguments[3] = Arg4;
        Report->SecondaryDataBytes = 0;
    }
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return (DXGK_DEBUG_REPORT_HANDLE)Report;
}

static BOOLEAN
DxgkpDbgReportSecondaryData(
    _Inout_ DXGK_DEBUG_REPORT_HANDLE Handle,
    _In_reads_bytes_(DataSize) PVOID Data,
    _In_ ULONG DataSize)
{
    PDXGKP_DEBUG_REPORT Report = (PDXGKP_DEBUG_REPORT)Handle;

    PAGED_CODE();

    if (Report == NULL || Data == NULL || DataSize == 0 ||
        DataSize > DXGK_DEBUG_REPORT_MAX_SIZE - Report->SecondaryDataBytes)
    {
        return FALSE;
    }
    Report->SecondaryDataBytes += DataSize;
    return TRUE;
}

static VOID
DxgkpDbgReportComplete(
    _Inout_ DXGK_DEBUG_REPORT_HANDLE Handle)
{
    PDXGKP_DEBUG_REPORT Report = (PDXGKP_DEBUG_REPORT)Handle;

    PAGED_CODE();

    if (Report == NULL)
        return;
    DXGKRNL_ERR("DbgReport: adapter %p code=0x%08lx completed with %lu secondary byte(s)\n",
                Report->Adapter, Report->Code, Report->SecondaryDataBytes);
    ExFreePoolWithTag(Report, TAG_DXGK_ADAPTER);
}

NTSTATUS
APIENTRY
DxgkCbQueryServices(
    _In_ HANDLE DeviceHandle,
    _In_ DXGK_SERVICES ServicesType,
    _Inout_ PINTERFACE Interface)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;
    LONG Count;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkCbQueryServices: handle=%p type=%lu iface=%p\n",
                  DeviceHandle, (ULONG)ServicesType, Interface);

    if (Interface == NULL)
        return STATUS_INVALID_PARAMETER;
    if (ServicesType < DxgkServicesAgp ||
        ServicesType > DxgkServicesFeature)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    Status = STATUS_NOT_SUPPORTED;

    if (ServicesType == DxgkServicesTimedOperation)
    {
        PDXGK_TIMED_OPERATION_INTERFACE TimedInterface =
            (PDXGK_TIMED_OPERATION_INTERFACE)Interface;
        DXGK_TIMED_OPERATION_INTERFACE ReturnedInterface;

        if (TimedInterface->Size < sizeof(ReturnedInterface))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            goto Done;
        }
        if (TimedInterface->Version != DXGK_TIMED_OPERATION_INTERFACE_VERSION_1)
            goto Done;

        RtlZeroMemory(&ReturnedInterface, sizeof(ReturnedInterface));
        ReturnedInterface.Size = sizeof(ReturnedInterface);
        ReturnedInterface.Version = DXGK_TIMED_OPERATION_INTERFACE_VERSION_1;
        ReturnedInterface.Context = Adapter;
        ReturnedInterface.InterfaceReference =
            DxgkpServicesInterfaceReferenceNop;
        ReturnedInterface.InterfaceDereference =
            DxgkpServicesInterfaceReferenceNop;
        ReturnedInterface.TimedOperationStart = DxgkpTimedOperationStart;
        ReturnedInterface.TimedOperationDelay = DxgkpTimedOperationDelay;
        ReturnedInterface.TimedOperationWaitForSingleObject =
            DxgkpTimedOperationWaitForSingleObject;
        *TimedInterface = ReturnedInterface;
        Status = STATUS_SUCCESS;
        goto Done;
    }

    if (ServicesType == DxgkServicesDebugReport)
    {
        PDXGK_DEBUG_REPORT_INTERFACE ReportInterface =
            (PDXGK_DEBUG_REPORT_INTERFACE)Interface;
        DXGK_DEBUG_REPORT_INTERFACE ReturnedInterface;

        if (ReportInterface->Size < sizeof(ReturnedInterface))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            goto Done;
        }
        if (ReportInterface->Version != DXGK_DEBUG_REPORT_INTERFACE_VERSION_1)
            goto Done;

        RtlZeroMemory(&ReturnedInterface, sizeof(ReturnedInterface));
        ReturnedInterface.Size = sizeof(ReturnedInterface);
        ReturnedInterface.Version = DXGK_DEBUG_REPORT_INTERFACE_VERSION_1;
        ReturnedInterface.Context = Adapter;
        ReturnedInterface.InterfaceReference =
            DxgkpServicesInterfaceReferenceNop;
        ReturnedInterface.InterfaceDereference =
            DxgkpServicesInterfaceReferenceNop;
        ReturnedInterface.DbgReportCreate = DxgkpDbgReportCreate;
        ReturnedInterface.DbgReportSecondaryData = DxgkpDbgReportSecondaryData;
        ReturnedInterface.DbgReportComplete = DxgkpDbgReportComplete;
        *ReportInterface = ReturnedInterface;
        Status = STATUS_SUCCESS;
        goto Done;
    }

#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    if (ServicesType == DxgkServicesFeature)
    {
        PDXGK_FEATURE_INTERFACE FeatureInterface =
            (PDXGK_FEATURE_INTERFACE)Interface;
        DXGK_FEATURE_INTERFACE ReturnedInterface;

        if (FeatureInterface->Size < sizeof(ReturnedInterface))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            goto Done;
        }
        if (FeatureInterface->Version !=
            DXGK_FEATURE_INTERFACE_VERSION_1)
        {
            goto Done;
        }

        RtlZeroMemory(&ReturnedInterface, sizeof(ReturnedInterface));
        ReturnedInterface.Size = sizeof(ReturnedInterface);
        ReturnedInterface.Version =
            DXGK_FEATURE_INTERFACE_VERSION_1;
        ReturnedInterface.Context = Adapter;
        ReturnedInterface.InterfaceReference =
            DxgkpFeatureInterfaceReferenceNop;
        ReturnedInterface.InterfaceDereference =
            DxgkpFeatureInterfaceReferenceNop;
        ReturnedInterface.IsFeatureEnabled =
            DxgkpFeatureIsEnabled;
        ReturnedInterface.QueryFeatureInterface =
            DxgkpFeatureQueryInterface;
        *FeatureInterface = ReturnedInterface;
        Status = STATUS_SUCCESS;
        goto Done;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    if (ServicesType == DxgkServicesFirmwareTable)
    {
        PDXGK_FIRMWARE_TABLE_INTERFACE FirmwareInterface =
            (PDXGK_FIRMWARE_TABLE_INTERFACE)Interface;
        DXGK_FIRMWARE_TABLE_INTERFACE ReturnedInterface;

        if (FirmwareInterface->Size < sizeof(ReturnedInterface))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            goto Done;
        }
        if (FirmwareInterface->Version !=
            DXGK_FIRMWARE_TABLE_INTERFACE_VERSION_1)
        {
            goto Done;
        }

        RtlZeroMemory(&ReturnedInterface, sizeof(ReturnedInterface));
        ReturnedInterface.Size = sizeof(ReturnedInterface);
        ReturnedInterface.Version =
            DXGK_FIRMWARE_TABLE_INTERFACE_VERSION_1;
        ReturnedInterface.Context = Adapter;
        ReturnedInterface.InterfaceReference =
            DxgkpFirmwareTableInterfaceReferenceNop;
        ReturnedInterface.InterfaceDereference =
            DxgkpFirmwareTableInterfaceReferenceNop;
        ReturnedInterface.EnumSystemFirmwareTables =
            DxgkpFirmwareTableEnumTables;
        ReturnedInterface.ReadSystemFirmwareTable =
            DxgkpFirmwareTableReadTable;
        *FirmwareInterface = ReturnedInterface;
        Status = STATUS_SUCCESS;
        goto Done;
    }
#endif

Done:
    /* Every service a miniport asks for during start is worth knowing about
     * when the start fails without explaining itself. */
    Count = InterlockedIncrement(&Adapter->QueryServicesCount);
    if (Count <= 16 || !NT_SUCCESS(Status))
    {
        DXGKRNL_INFO("DxgkCbQueryServices #%ld: adapter=%p type=%lu size=%u "
                    "version=%u -> status=0x%08lx\n",
                    Count, Adapter, (ULONG)ServicesType,
                    Interface->Size, Interface->Version, Status);
    }
    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return Status;
}

/*
 * DxgkCbSynchronizeExecution
 *
 * Runs SynchronizeRoutine in the context of the adapter's interrupt ISR
 * (i.e. at the interrupt's IRQL with the interrupt spinlock held), then
 * returns the routine's boolean result in *ReturnValue.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
NTSTATUS
APIENTRY
DxgkCbSynchronizeExecution(
    _In_  HANDLE                  DeviceHandle,
    _In_  PKSYNCHRONIZE_ROUTINE   SynchronizeRoutine,
    _In_  PVOID                   Context,
    _In_  ULONG                   MessageNumber,
    _Out_ PBOOLEAN                ReturnValue)
{
    PDXGKRNL_ADAPTER Adapter;
    PKINTERRUPT InterruptObject = NULL;
    ULONGLONG        TotalStart100ns;
    ULONGLONG        ElapsedUs;

    TotalStart100ns = DxgkpTraceNow100ns();

    if (ReturnValue == NULL || SynchronizeRoutine == NULL)
    {
        if (ReturnValue != NULL)
            *ReturnValue = FALSE;
        return STATUS_INVALID_PARAMETER;
    }

    *ReturnValue = FALSE;

    Adapter = DxgkpHandleToAdapter(DeviceHandle);
    if (Adapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbSynchronizeExecution: invalid handle %p\n",
                    DeviceHandle);
        return STATUS_INVALID_HANDLE;
    }

    if (Adapter->InterruptMessageTable != NULL)
    {
        if (MessageNumber >= Adapter->InterruptMessageTable->MessageCount)
        {
            ExReleaseRundownProtection(
                &Adapter->ReverseCallbackRundownRef);
            return STATUS_INVALID_PARAMETER;
        }
        InterruptObject =
            Adapter->InterruptMessageTable->MessageInfo[MessageNumber].InterruptObject;
    }
    else
    {
        if (MessageNumber != 0)
        {
            ExReleaseRundownProtection(
                &Adapter->ReverseCallbackRundownRef);
            return STATUS_INVALID_PARAMETER;
        }
        InterruptObject = Adapter->InterruptObject;
    }

    if (InterruptObject == NULL)
    {
        ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
        return STATUS_UNSUCCESSFUL;
    }

    *ReturnValue = KeSynchronizeExecution(InterruptObject,
                                          SynchronizeRoutine,
                                          Context);

    ElapsedUs = DxgkpTraceElapsedUs(TotalStart100ns);
    if (ElapsedUs >= DXGK_TRACE_SLOW_SYNC_US)
    {
        DXGKRNL_WARN("DxgkCbSynchronizeExecution: slow sync Message=%lu Interrupt=%p took %I64u us\n",
                     MessageNumber, InterruptObject, ElapsedUs);
    }

    ExReleaseRundownProtection(&Adapter->ReverseCallbackRundownRef);
    return STATUS_SUCCESS;
}

static BOOLEAN
DxgkpValidatePostDisplayInformation(
    _In_ const DXGK_DISPLAY_INFORMATION *DisplayInformation)
{
    ULONG BytesPerPixel = 4;

    if (DisplayInformation->Width == 0)
        return TRUE;
    if (DisplayInformation->Height == 0 ||
        DisplayInformation->PhysicAddress.QuadPart <= 0 ||
        (DisplayInformation->ColorFormat != D3DDDIFMT_X8R8G8B8 &&
         DisplayInformation->ColorFormat != D3DDDIFMT_A8R8G8B8) ||
        DisplayInformation->Width > MAXULONG / BytesPerPixel ||
        DisplayInformation->Pitch <
            DisplayInformation->Width * BytesPerPixel ||
        DisplayInformation->Height >
            MAXULONG_PTR / DisplayInformation->Pitch)
    {
        return FALSE;
    }
    return TRUE;
}

/*
 * DxgkpAcquirePostDisplayOwnership
 *
 * Called by the miniport during DxgkDdiStartDevice to obtain POST state from
 * the previous WDDM owner or firmware and claim display ownership.
 *
 * If no representable POST framebuffer is present, all fields are zeroed and
 * STATUS_SUCCESS is returned so the miniport cold-starts its pipeline.
 *
 * IRQL: PASSIVE_LEVEL
 */

static NTSTATUS
DxgkpAcquirePostDisplayOwnership(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInformation,
    _Out_opt_ PDXGK_DISPLAY_OWNERSHIP_FLAGS Flags)
{
    PDXGKRNL_ADAPTER            Claimant;
    PDXGKRNL_ADAPTER            PreviousOwner = NULL;
    LONG                        AcquireCount;
    LOADER_PARAMETER_FRAMEBUFFER Fb;
    DXGK_DISPLAY_INFORMATION     ReleasedDisplayInformation;
    DXGK_FRAMEBUFFER_STATE       FrameBufferState = FrameBufferStateUnknown;
    D3DDDIFORMAT                 ColorFormat;
    ULONG                        BytesPerPixel;
    ULONGLONG                    TotalStart100ns;
    ULONGLONG                    GopQueryUs = 0;
    ULONGLONG                    OwnershipUs = 0;
    ULONGLONG                    StepStart100ns;
    BOOLEAN                      ReleasedByDriver = FALSE;
    BOOLEAN                      TransferFromInbv = TRUE;
    NTSTATUS                     Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (DisplayInformation == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Flags != NULL)
        RtlZeroMemory(Flags, sizeof(*Flags));

    Claimant = DxgkpHandleToAdapter(DeviceHandle);
    if (Claimant == NULL)
        return STATUS_INVALID_HANDLE;

    TotalStart100ns = DxgkpTraceNow100ns();

    DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: handle=%p out=%p\n",
                  DeviceHandle, DisplayInformation);

    RtlZeroMemory(DisplayInformation, sizeof(*DisplayInformation));
    RtlZeroMemory(&ReleasedDisplayInformation,
                  sizeof(ReleasedDisplayInformation));
    (VOID)KeWaitForSingleObject(&g_PostDisplayOwnershipMutex, Executive, KernelMode, FALSE, NULL);

    /*
     * Boot display ownership policy (the MSBDD handover, see
     * g_PostDisplayOwnerAdapter above):
     *   - only the first real miniport may replace the basic-display
     *     fallback;
     *   - a later adapter gets an empty descriptor and leaves the established
     *     real display owner running, as native does for a non-POST device.
     */
    {
        PDEVICE_OBJECT OwnerDeviceObject;
        PDXGKRNL_ADAPTER Owner = DxgkpReferencePostDisplayOwner(&OwnerDeviceObject);
        BOOLEAN RetainFallback = FALSE;

        PreviousOwner = Owner;
        if (Claimant != NULL && Owner != NULL && Owner != Claimant)
        {
            RetainFallback =
                Claimant->MiniportContext != NULL &&
                !Claimant->MiniportContext->IsBasicDisplayFallback &&
                Owner->MiniportContext != NULL &&
                Owner->MiniportContext->IsBasicDisplayFallback;
            if (!RetainFallback)
            {
                DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: boot display "
                              "already owned by adapter %p; claimant %p gets "
                              "an empty descriptor\n",
                              Owner,
                              Claimant);
                ObDereferenceObject(OwnerDeviceObject);
                goto Complete;
            }

            /*
             * An empty POST descriptor still permits a real miniport to
             * cold-start its display pipeline.  Native
             * DpiAcquirePostDisplayOwnership returns the empty descriptor for
             * a non-POST device; it does not make the software fallback the
             * permanent display owner.  ReactOS has one win32ss bridge, so
             * hand that bridge to the first full miniport even when the
             * firmware supplied no framebuffer.  The retained fallback is
             * restarted if claimant startup subsequently fails.
             */
            Status = DxgkpRetainAndStopBasicDisplayFallback(
                         Claimant,
                         Owner,
                         &OwnerDeviceObject,
                         &ReleasedDisplayInformation,
                         &ReleasedByDriver);
            if (!NT_SUCCESS(Status))
            {
                if (OwnerDeviceObject != NULL)
                    ObDereferenceObject(OwnerDeviceObject);
                goto Complete;
            }
        }
        if (OwnerDeviceObject != NULL)
            ObDereferenceObject(OwnerDeviceObject);
    }

    if (ReleasedByDriver)
    {
        TransferFromInbv = FALSE;
        if (!DxgkpValidatePostDisplayInformation(
                 &ReleasedDisplayInformation))
        {
            DXGKRNL_WARN("DxgkCbAcquirePostDisplayOwnership: previous KMD "
                         "returned invalid POST information\n");
            RtlZeroMemory(&ReleasedDisplayInformation,
                          sizeof(ReleasedDisplayInformation));
        }
        else if (ReleasedDisplayInformation.Width != 0)
        {
            *DisplayInformation = ReleasedDisplayInformation;
            FrameBufferState = FrameBufferStateInitializedByDriver;
        }
        goto StorePostDisplay;
    }

    /*
     * If no valid GOP framebuffer was saved by FreeLOADer / InbV the
     * miniport must initialise its pipeline from scratch.  Return a
     * zeroed structure with STATUS_SUCCESS.
     */
    if (!InbvHasValidGopFrameBuffer())
    {
        DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                      "no firmware framebuffer; miniport must cold-start\n");
        goto TransferOwnership;
    }

    StepStart100ns = DxgkpTraceNow100ns();
    if (!InbvGetGopFrameBufferInfo(&Fb))
    {
        DXGKRNL_ERR("DxgkCbAcquirePostDisplayOwnership: "
                    "InbvGetGopFrameBufferInfo failed\n");
        goto TransferOwnership;
    }
    GopQueryUs = DxgkpTraceElapsedUs(StepStart100ns);

    /*
     * DXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP permits only the X8/A8 R8G8B8
     * formats.  Publish only firmware layouts that can be represented exactly
     * as X8R8G8B8; a zero descriptor tells KMD to cold-start every other mode.
     * FreeLoader stores bits-per-pixel in PixelFormat, while an EFI loader may
     * store the GOP enum.  The masks disambiguate the former convention.
     */
    switch (Fb.PixelFormat)
    {
        case 1: /* EFI PixelBlueGreenRedReserved8BitPerColor */
            ColorFormat   = D3DDDIFMT_X8R8G8B8;
            BytesPerPixel = 4;
            break;

        case 2: /* EFI PixelBitMask */
            if (Fb.RedMask == 0x00FF0000 &&
                Fb.GreenMask == 0x0000FF00 &&
                Fb.BlueMask == 0x000000FF)
            {
                ColorFormat = D3DDDIFMT_X8R8G8B8;
                BytesPerPixel = 4;
            }
            else
            {
                DXGKRNL_WARN("DxgkCbAcquirePostDisplayOwnership: "
                             "unrepresentable GOP masks R=%08lX G=%08lX "
                             "B=%08lX\n",
                             Fb.RedMask,
                             Fb.GreenMask,
                             Fb.BlueMask);
                goto TransferOwnership;
            }
            break;

        /* FreeLoader bits-per-pixel convention. */
        case 32:
            if (Fb.RedMask != 0x00FF0000 ||
                Fb.GreenMask != 0x0000FF00 ||
                Fb.BlueMask != 0x000000FF)
            {
                DXGKRNL_WARN("DxgkCbAcquirePostDisplayOwnership: "
                             "unrepresentable 32-bpp masks R=%08lX G=%08lX "
                             "B=%08lX\n",
                             Fb.RedMask,
                             Fb.GreenMask,
                             Fb.BlueMask);
                goto TransferOwnership;
            }
            ColorFormat = D3DDDIFMT_X8R8G8B8;
            BytesPerPixel = 4;
            break;

        default: /* RGBX, 15/16-bpp, BltOnly, or unknown. */
            DXGKRNL_WARN("DxgkCbAcquirePostDisplayOwnership: "
                         "PixelFormat %lu is not representable by the public "
                         "POST format contract\n",
                         Fb.PixelFormat);
            goto TransferOwnership;
    }

    if (Fb.HorizontalResolution == 0 ||
        Fb.VerticalResolution == 0 ||
        Fb.FrameBufferBase.QuadPart <= 0 ||
        Fb.PixelsPerScanLine < Fb.HorizontalResolution ||
        Fb.PixelsPerScanLine > MAXULONG / BytesPerPixel ||
        Fb.VerticalResolution >
            MAXULONG_PTR / (Fb.PixelsPerScanLine * BytesPerPixel))
    {
        DXGKRNL_WARN("DxgkCbAcquirePostDisplayOwnership: invalid firmware "
                     "geometry %lux%lu scan=%lu PA=0x%I64X\n",
                     Fb.HorizontalResolution,
                     Fb.VerticalResolution,
                     Fb.PixelsPerScanLine,
                     Fb.FrameBufferBase.QuadPart);
        goto TransferOwnership;
    }

    DisplayInformation->Width         = Fb.HorizontalResolution;
    DisplayInformation->Height        = Fb.VerticalResolution;
    DisplayInformation->Pitch         = Fb.PixelsPerScanLine * BytesPerPixel;
    DisplayInformation->ColorFormat   = ColorFormat;
    DisplayInformation->PhysicAddress.QuadPart = Fb.FrameBufferBase.QuadPart;
    DisplayInformation->TargetId      = D3DDDI_ID_UNINITIALIZED;
    DisplayInformation->AcpiId        = 0;
    FrameBufferState = FrameBufferStateInitializedByFirmware;

    /* Store POST display info in the adapter for later use. */
StorePostDisplay:
    if (DisplayInformation->Width != 0)
    {
        PDXGKRNL_ADAPTER PostAdapter = Claimant;
        if (PostAdapter != NULL)
        {
            SIZE_T FbSize = (SIZE_T)DisplayInformation->Pitch *
                            DisplayInformation->Height;
            BOOLEAN MappingChanged;

            MappingChanged =
                (PostAdapter->PostDisplayVirtualAddress != NULL) &&
                (PostAdapter->PostDisplayPhysicalAddress.QuadPart !=
                     DisplayInformation->PhysicAddress.QuadPart ||
                 PostAdapter->PostDisplayPitch != DisplayInformation->Pitch ||
                 PostAdapter->PostDisplayHeight != DisplayInformation->Height);

            if (MappingChanged)
            {
                DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                              "remapping GOP FB old PA=0x%I64X Pitch=%lu Height=%lu "
                              "new PA=0x%I64X Pitch=%lu Height=%lu\n",
                              PostAdapter->PostDisplayPhysicalAddress.QuadPart,
                              PostAdapter->PostDisplayPitch,
                              PostAdapter->PostDisplayHeight,
                              DisplayInformation->PhysicAddress.QuadPart,
                              DisplayInformation->Pitch,
                              DisplayInformation->Height);
                DxgkpReleasePostDisplayMapping(PostAdapter);
            }

            PostAdapter->PostDisplayWidth  = DisplayInformation->Width;
            PostAdapter->PostDisplayHeight = DisplayInformation->Height;
            PostAdapter->PostDisplayPhysicalAddress = DisplayInformation->PhysicAddress;
            PostAdapter->PostDisplayPitch  = DisplayInformation->Pitch;

            /* Map the transferred POST framebuffer for direct CPU access. */
            if (DisplayInformation->PhysicAddress.QuadPart != 0 &&
                PostAdapter->PostDisplayVirtualAddress == NULL)
            {
                /* Try write-combined first (standard for framebuffers),
                 * fall back to non-cached. */
                PostAdapter->PostDisplayVirtualAddress =
                    MmMapIoSpace(DisplayInformation->PhysicAddress,
                                 FbSize,
                                 MmWriteCombined);
                if (PostAdapter->PostDisplayVirtualAddress == NULL)
                {
                    PostAdapter->PostDisplayVirtualAddress =
                        MmMapIoSpace(DisplayInformation->PhysicAddress,
                                     FbSize,
                                     MmNonCached);
                }

                if (PostAdapter->PostDisplayVirtualAddress != NULL)
                {
                    PostAdapter->PostDisplayMappingSize = FbSize;
                    ASSERT(PostAdapter->PostDisplayMappingSize == FbSize);
                }

                DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                              "mapped POST FB PA=0x%I64X -> VA=%p (%Iu bytes)\n",
                              DisplayInformation->PhysicAddress.QuadPart,
                              PostAdapter->PostDisplayVirtualAddress,
                              FbSize);
            }
        }
    }

TransferOwnership:
    DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: "
                  "%lux%lu Pitch=%lu Fmt=%d PA=0x%I64X\n",
                  DisplayInformation->Width,
                  DisplayInformation->Height,
                  DisplayInformation->Pitch,
                  (int)DisplayInformation->ColorFormat,
                  DisplayInformation->PhysicAddress.QuadPart);

    /*
     * Transfer display ownership from InbV to the miniport.
     * This synchronously stops the boot animation before InbV marks the
     * display lost, so no boot-video thread can write after the handoff.
     */
    if (TransferFromInbv)
    {
        StepStart100ns = DxgkpTraceNow100ns();
        InbvNotifyDisplayOwnershipLost(NULL);
        OwnershipUs = DxgkpTraceElapsedUs(StepStart100ns);
    }

    DxgkpSetPostDisplayOwner(Claimant);

    DXGKRNL_TRACE("DxgkCbAcquirePostDisplayOwnership: gop=%I64u us ownership=%I64u us total=%I64u us\n",
                  GopQueryUs,
                  OwnershipUs,
                  DxgkpTraceElapsedUs(TotalStart100ns));

Complete:
    if (Flags != NULL && NT_SUCCESS(Status))
        Flags->FrameBufferState = FrameBufferState;
    /* Whether a miniport claimed the boot display, and what it was
     * handed, decides which adapter drives the desktop bridge. */
    AcquireCount = InterlockedIncrement(&Claimant->PostDisplayAcquireCount);
    if (AcquireCount <= 8 || !NT_SUCCESS(Status))
    {
        DXGKRNL_INFO("DxgkCbAcquirePostDisplayOwnership #%ld: claimant=%p "
                    "owner=%p -> status=0x%08lx %lux%lu pitch=%lu fmt=%d "
                    "pa=0x%I64x fbstate=%d\n",
                    AcquireCount, Claimant, PreviousOwner, Status,
                    DisplayInformation->Width, DisplayInformation->Height,
                    DisplayInformation->Pitch,
                    (int)DisplayInformation->ColorFormat,
                    DisplayInformation->PhysicAddress.QuadPart,
                    (int)FrameBufferState);
    }
    KeReleaseMutex(&g_PostDisplayOwnershipMutex, FALSE);
    ExReleaseRundownProtection(&Claimant->ReverseCallbackRundownRef);
    return Status;
}

NTSTATUS
APIENTRY
DxgkCbAcquirePostDisplayOwnership(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInformation)
{
    return DxgkpAcquirePostDisplayOwnership(DeviceHandle,
                                             DisplayInformation,
                                             NULL);
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
NTSTATUS
APIENTRY
DxgkCbAcquirePostDisplayOwnership2(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInformation,
    _Out_ PDXGK_DISPLAY_OWNERSHIP_FLAGS Flags)
{
    if (DisplayInformation == NULL || Flags == NULL)
        return STATUS_INVALID_PARAMETER;

    return DxgkpAcquirePostDisplayOwnership(DeviceHandle,
                                             DisplayInformation,
                                             Flags);
}
#endif

static BOOLEAN
DxgkpInvokeMiniportInterrupt(
    _In_opt_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG MessageNumber,
    _In_ PCSTR IsrName)
{
    BOOLEAN   Handled;
    LONG      Sequence;
    BOOLEAN   Logged;
    ULONGLONG Start100ns;
    ULONGLONG ElapsedUs;
    LONG      NotifyBefore;

    if (Adapter == NULL)
        return FALSE;
    if (!DxgkpAcquireVidSchCallback(Adapter))
        return FALSE;
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->InitData.s.DxgkDdiInterruptRoutine == NULL)
    {
        DxgkpReleaseVidSchCallback(Adapter);
        return FALSE;
    }
    if (!DxgkAcquireInterruptCallback(Adapter))
    {
        DxgkpReleaseVidSchCallback(Adapter);
        return FALSE;
    }

    Sequence = InterlockedIncrement(&Adapter->InterruptCount);
    Logged = (Sequence <= DXGK_TRACE_ISR_LOG_LIMIT);
    Start100ns = DxgkpTraceNow100ns();
    NotifyBefore = Adapter->NotifyTotalCount;

    Handled = Adapter->MiniportContext->InitData.s.DxgkDdiInterruptRoutine(Adapter->MiniportDeviceContext, MessageNumber);
    DxgkReleaseInterruptCallback(Adapter);

    /* TDR diagnostics: an interrupt the miniport consumed without telling
     * the scheduler anything is worth knowing about when a fence stalls. */
    InterlockedExchange64(&Adapter->LastIsrTime100ns, (LONG64)DxgkDiagNow100ns());
    if (Adapter->NotifyTotalCount == NotifyBefore)
    {
        InterlockedIncrement(&Adapter->OtherIsrCount);
        InterlockedExchange64(&Adapter->LastOtherIsrTime100ns, (LONG64)DxgkDiagNow100ns());
        InterlockedExchange(&Adapter->LastOtherIsrMessage, (LONG)MessageNumber);
    }
    if (!Handled)
        InterlockedIncrement(&Adapter->UnhandledIsrCount);

    ElapsedUs = DxgkpTraceElapsedUs(Start100ns);

    if (Logged)
    {
        DXGKRNL_TRACE("%s: seq=%ld msg=%lu handled=%d state=%d queue=%ld dpc=%ld t+%I64u us dur=%I64u us\n",
                      IsrName,
                      Sequence,
                      MessageNumber,
                      Handled,
                      Adapter->State,
                      Adapter->QueueDpcCount,
                      Adapter->DpcCount,
                      DxgkpTraceSinceStartUs(Adapter),
                      ElapsedUs);
    }

    DxgkpReleaseVidSchCallback(Adapter);
    return Handled;
}

/*
 * DxgkpIsrTrampoline — ISR wrapper for KSERVICE_ROUTINE→DxgkDdiInterruptRoutine
 */
static BOOLEAN NTAPI
DxgkpIsrTrampoline(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID       ServiceContext)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)ServiceContext;
    UNREFERENCED_PARAMETER(Interrupt);

    return DxgkpInvokeMiniportInterrupt(Adapter,
                                        0 /* MessageNumber */,
                                        "DxgkpIsrTrampoline");
}

/*
 * DxgkpMessageIsrTrampoline — MSI/MSI-X ISR wrapper for
 * PKMESSAGE_SERVICE_ROUTINE→DxgkDdiInterruptRoutine.
 */
static BOOLEAN NTAPI
DxgkpMessageIsrTrampoline(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID       ServiceContext,
    _In_ ULONG       MessageNumber)
{
    PDXGKRNL_ADAPTER Adapter = (PDXGKRNL_ADAPTER)ServiceContext;
    UNREFERENCED_PARAMETER(Interrupt);

    return DxgkpInvokeMiniportInterrupt(Adapter,
                                        MessageNumber,
                                        "DxgkpMessageIsrTrampoline");
}

/*
 * DxgkpQueryDriverCaps — DXGKQAITYPE_DRIVERCAPS into a caller buffer of
 * DXGKP_DRIVERCAPS_QUERY_SIZE bytes (see dxgkrnl_private.h).
 *
 * First asks with our own sizeof; if the miniport was built against a
 * newer WDK whose DXGK_DRIVERCAPS is bigger it fails the size check
 * (STATUS_BUFFER_TOO_SMALL/INVALID_PARAMETER) — retry with the large
 * zeroed buffer so callers can read the stable head fields.
 */
NTSTATUS
DxgkpQueryDriverCaps(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_writes_bytes_(DXGKP_DRIVERCAPS_QUERY_SIZE) PDXGK_DRIVERCAPS Caps)
{
    PDXGKDDI_QUERY_ADAPTER_INFO PfnQueryAdapterInfo;
    DXGKARG_QUERYADAPTERINFO QueryArgs;
    ULONG Attempt;
    NTSTATUS Status;

    if (Caps == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Caps, DXGKP_DRIVERCAPS_QUERY_SIZE);

    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;

    PfnQueryAdapterInfo = DXGK_CB(Adapter, DxgkDdiQueryAdapterInfo);
    if (PfnQueryAdapterInfo == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;

    Status = STATUS_UNSUCCESSFUL;
    for (Attempt = 0; Attempt < 2; Attempt++)
    {
        RtlZeroMemory(Caps, DXGKP_DRIVERCAPS_QUERY_SIZE);
        RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
        QueryArgs.Type = DXGKQAITYPE_DRIVERCAPS;
        QueryArgs.pOutputData = Caps;
        QueryArgs.OutputDataSize = (Attempt == 0) ? sizeof(DXGK_DRIVERCAPS)
                                                  : DXGKP_DRIVERCAPS_QUERY_SIZE;

        _SEH2_TRY
        {
            Status = PfnQueryAdapterInfo(Adapter->MiniportDeviceContext, &QueryArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (NT_SUCCESS(Status))
            break;
    }

    DxgkReleaseKmdCall(Adapter);
    return Status;
}

NTSTATUS
DxgkpQueryGpuMmuCaps(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ DXGK_GPUMMUCAPS *Caps)
{
    PDXGKDDI_QUERY_ADAPTER_INFO PfnQueryAdapterInfo;
    DXGKARG_QUERYADAPTERINFO QueryArgs;
    DXGK_QUERYGPUMMUCAPSIN CapsIn;
    NTSTATUS Status;

    if (Caps == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Caps, sizeof(*Caps));
    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;

    PfnQueryAdapterInfo = DXGK_CB(Adapter, DxgkDdiQueryAdapterInfo);
    if (PfnQueryAdapterInfo == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;

    /* The query names the physical adapter it is about; a miniport that
     * supports linked adapters rejects the request without it. */
    RtlZeroMemory(&CapsIn, sizeof(CapsIn));
    CapsIn.PhysicalAdapterIndex = 0;
    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.Type = DXGKQAITYPE_GPUMMUCAPS;
    QueryArgs.pInputData = &CapsIn;
    QueryArgs.InputDataSize = sizeof(CapsIn);
    QueryArgs.pOutputData = Caps;
    QueryArgs.OutputDataSize = sizeof(*Caps);

    _SEH2_TRY
    {
        Status = PfnQueryAdapterInfo(Adapter->MiniportDeviceContext, &QueryArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    DxgkReleaseKmdCall(Adapter);
    return Status;
}

/*
 * DxgkpQueryPageTableLevelDesc
 *
 * Reads one level of the miniport's page-table geometry.  Level 0 is the leaf.
 */
NTSTATUS
DxgkpQueryPageTableLevelDesc(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG LevelIndex,
    _Out_ DXGK_PAGE_TABLE_LEVEL_DESC *Desc)
{
    PDXGKDDI_QUERY_ADAPTER_INFO PfnQueryAdapterInfo;
    DXGKARG_QUERYADAPTERINFO QueryArgs;
    DXGK_QUERYPAGETABLELEVELDESCIN Input;
    NTSTATUS Status;

    if (Desc == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Desc, sizeof(*Desc));
    if (Adapter == NULL || Adapter->MiniportContext == NULL || LevelIndex > MAXUSHORT)
        return STATUS_INVALID_PARAMETER;

    PfnQueryAdapterInfo = DXGK_CB(Adapter, DxgkDdiQueryAdapterInfo);
    if (PfnQueryAdapterInfo == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.LevelIndex = (WORD)LevelIndex;
    Input.PhysicalAdapterIndex = 0;
    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.Type = DXGKQAITYPE_PAGETABLELEVELDESC;
    QueryArgs.pInputData = &Input;
    QueryArgs.InputDataSize = sizeof(Input);
    QueryArgs.pOutputData = Desc;
    QueryArgs.OutputDataSize = sizeof(*Desc);

    _SEH2_TRY
    {
        Status = PfnQueryAdapterInfo(Adapter->MiniportDeviceContext, &QueryArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    DxgkReleaseKmdCall(Adapter);
    return Status;
}

/*
 * DxgkpCacheGpuMmuGeometry
 *
 * Validates and caches every page-table level the miniport declares.  A level
 * whose index bit count, size, or alignment is inconsistent disables the whole
 * GPU virtual-memory model rather than leaving a partially-derived geometry
 * that later code would have to guess about.
 */
static BOOLEAN
DxgkpCacheGpuMmuGeometry(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG TotalIndexBits = 0;
    ULONG Level;

    if (Adapter->GpuMmuCaps.PageTableLevelCount == 0 ||
        Adapter->GpuMmuCaps.PageTableLevelCount > DXGK_MAX_PAGE_TABLE_LEVELS)
    {
        return FALSE;
    }

    for (Level = 0; Level < Adapter->GpuMmuCaps.PageTableLevelCount; ++Level)
    {
        DXGK_PAGE_TABLE_LEVEL_DESC *Desc = &Adapter->PageTableLevels[Level];
        ULONGLONG Entries;
        NTSTATUS QueryStatus;

        QueryStatus = DxgkpQueryPageTableLevelDesc(Adapter, Level, Desc);
        if (!NT_SUCCESS(QueryStatus))
        {
            DPRINT1("DxgkpCacheGpuMmuGeometry: level %lu query failed 0x%08lx\n",
                    Level,
                    QueryStatus);
            return FALSE;
        }
        if (Desc->PageTableIndexBitCount == 0 || Desc->PageTableIndexBitCount >= 32)
        {
            DPRINT1("DxgkpCacheGpuMmuGeometry: invalid level %lu index bits\n", Level);
            return FALSE;
        }
        if (Desc->PageTableSizeInBytes == 0)
        {
            DPRINT1("DxgkpCacheGpuMmuGeometry: invalid level %lu table size\n", Level);
            return FALSE;
        }
        /* Zero alignment means the memory segment's page size. */
        if (Desc->PageTableAlignmentInBytes == 0)
            Desc->PageTableAlignmentInBytes = PAGE_SIZE;
        if ((Desc->PageTableAlignmentInBytes & (Desc->PageTableAlignmentInBytes - 1)) != 0)
        {
            DPRINT1("DxgkpCacheGpuMmuGeometry: invalid level %lu alignment\n", Level);
            return FALSE;
        }
        Entries = 1ULL << Desc->PageTableIndexBitCount;
        /*
         * PageTableSegmentId decides where a page table has to live: zero
         * means system memory addressed physically, nonzero names the segment
         * the table must be placed in, which changes both the root address
         * published through DxgkDdiSetRootPageTable and how a parent entry
         * has to describe its child.  Report the geometry the miniport asked
         * for so the two cannot be guessed at separately.
         */
        DXGKRNL_INFO("DxgkAdapterStart: page-table level %lu: %I64u entries x %u bytes "
                     "(index-bits=%u align=%u) segment=%u paging-process-segment=%u\n",
                     Level,
                     Entries,
                     Desc->PageTableSizeInBytes,
                     Desc->PageTableIndexBitCount,
                     Desc->PageTableAlignmentInBytes,
                     Desc->PageTableSegmentId,
                     Desc->PagingProcessPageTableSegmentId);
        TotalIndexBits += Desc->PageTableIndexBitCount;
    }

    /* Every VA bit above the page offset must be covered by exactly the
     * declared levels, otherwise a translation would be ambiguous. */
    if (TotalIndexBits + 12 != Adapter->GpuMmuCaps.VirtualAddressBitCount)
    {
        DPRINT1("DxgkpCacheGpuMmuGeometry: index total %lu does not match VA bits %u\n",
                TotalIndexBits + 12,
                Adapter->GpuMmuCaps.VirtualAddressBitCount);
        return FALSE;
    }

    Adapter->PageTableLevelsValid = TRUE;
    return TRUE;
}

/* ========================================================================
 * Adapter lifecycle functions
 * ====================================================================== */

static ULONG
DxgkpMms2GetAdapterFlags(_In_ PDXGKRNL_ADAPTER Adapter)
{
    return Adapter->MiniportContext->IsDisplayOnlyDriver ? DXGMMS2_ADAPTER_FLAG_DISPLAY_ONLY : 0;
}

static DECLSPEC_NORETURN VOID
DxgkpBugCheckMms2Lifecycle(_In_ PDXGKRNL_ADAPTER Adapter, _In_ NTSTATUS FailureStatus, _In_ ULONG Phase)
{
    InterlockedExchange(&Adapter->KmdCallsBlocked, 1);
    InterlockedExchange(&Adapter->InterruptCallbacksBlocked, 1);
    InterlockedExchange(&Adapter->MiniportCallbacksValid, 0);
    KeMemoryBarrier();
    DXGKRNL_ERR("DxgkpBugCheckMms2Lifecycle: adapter %p cannot contain dxgmms2 lifecycle failure, status 0x%08lX phase %lu\n", Adapter, FailureStatus, Phase);
    KeBugCheckEx(DXGKP_BUGCHECK_VIDEO_DXGKRNL_FATAL_ERROR, (ULONG_PTR)DXGKP_FATAL_MMS2_LIFECYCLE_SUBTYPE, (ULONG_PTR)FailureStatus, (ULONG_PTR)Adapter, (ULONG_PTR)Phase);
}

static VOID
DxgkpMms2PublishStarted(_In_ PDXGKRNL_ADAPTER Adapter)
{
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Adapter->Mms2State == DxgkMms2AdapterCreated || Adapter->Mms2State == DxgkMms2AdapterStopped);
    Adapter->Mms2State = DxgkMms2AdapterStarted;
    Adapter->Mms2StopReason = 0;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
}

static VOID
DxgkpMms2PublishContextStreamInterface(_In_ PDXGKRNL_ADAPTER Adapter, _In_ const DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *ContextStreamInterface)
{
    ASSERT(Adapter != NULL);
    ASSERT(ContextStreamInterface != NULL);
    ASSERT(InterlockedCompareExchange(&Adapter->Mms2ContextStreamValid, 0, 0) == 0);
    Adapter->Mms2ContextStreamInterface = *ContextStreamInterface;
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->Mms2ContextStreamValid, 1);
}

static VOID
DxgkpMms2UnpublishContextStreamInterface(_In_ PDXGKRNL_ADAPTER Adapter)
{
    ASSERT(Adapter != NULL);
    InterlockedExchange(&Adapter->Mms2ContextStreamValid, 0);
    KeMemoryBarrier();
}

static VOID
DxgkpMms2ClearContextStreamInterface(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DxgkpMms2UnpublishContextStreamInterface(Adapter);
    RtlZeroMemory(&Adapter->Mms2ContextStreamInterface, sizeof(Adapter->Mms2ContextStreamInterface));
}

static NTSTATUS
DxgkpMms2BeginStop(_In_ PDXGKRNL_ADAPTER Adapter, _In_ DXGMMS2_STOP_REASON RequestedReason)
{
    DXGMMS2_ADAPTER_HANDLE Mms2Adapter;
    BOOLEAN ContextStreamWasPublished;
    NTSTATUS Status;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2State == DxgkMms2AdapterBeginPending || Adapter->Mms2State == DxgkMms2AdapterStopping || Adapter->Mms2State == DxgkMms2AdapterCreated || Adapter->Mms2State == DxgkMms2AdapterStopped)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (Adapter->Mms2State != DxgkMms2AdapterStarted || Adapter->Mms2Adapter == NULL)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Mms2Adapter = Adapter->Mms2Adapter;
    ContextStreamWasPublished = InterlockedCompareExchange(&Adapter->Mms2ContextStreamValid, 0, 0) != 0;
    DxgkpMms2UnpublishContextStreamInterface(Adapter);
    Adapter->Mms2State = DxgkMms2AdapterBeginPending;
    Adapter->Mms2StopReason = RequestedReason;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    Status = DxgkpMms2BeginStopAdapter(Mms2Adapter, RequestedReason);
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2Adapter == Mms2Adapter && Adapter->Mms2State == DxgkMms2AdapterBeginPending && Adapter->Mms2StopReason == RequestedReason)
    {
        Adapter->Mms2State = NT_SUCCESS(Status) ? DxgkMms2AdapterStopping : DxgkMms2AdapterStarted;
        if (!NT_SUCCESS(Status))
        {
            Adapter->Mms2StopReason = 0;
            if (ContextStreamWasPublished)
            {
                KeMemoryBarrier();
                InterlockedExchange(&Adapter->Mms2ContextStreamValid, 1);
            }
        }
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    return Status;
}

static NTSTATUS
DxgkpMms2CompleteRetiredStop(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGMMS2_ADAPTER_HANDLE Mms2Adapter;
    DXGMMS2_STOP_REASON Reason;
    BOOLEAN TimelineWasPublished;
    NTSTATUS Status;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2State == DxgkMms2AdapterCreated || Adapter->Mms2State == DxgkMms2AdapterStopped)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (Adapter->Mms2State != DxgkMms2AdapterStopping || Adapter->Mms2Adapter == NULL)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Mms2Adapter = Adapter->Mms2Adapter;
    Reason = Adapter->Mms2StopReason;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    TimelineWasPublished = DxgkpCloseMms2TimelineCalls(Adapter);
    if (!TimelineWasPublished && InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0)
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    Status = DxgkpMms2CompleteStopAdapter(Mms2Adapter, Reason);
    if (TimelineWasPublished)
        DxgkpReopenMms2TimelineCalls(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    DxgkpMms2ClearContextStreamInterface(Adapter);
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2Adapter == Mms2Adapter && Adapter->Mms2State == DxgkMms2AdapterStopping && Adapter->Mms2StopReason == Reason)
    {
        Adapter->Mms2State = DxgkMms2AdapterStopped;
        Adapter->Mms2StopReason = 0;
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpMms2DestroyAdministrativeAdapter(_In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGMMS2_ADAPTER_HANDLE Mms2Adapter;
    BOOLEAN TimelineWasPublished;
    NTSTATUS Status;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2Adapter == NULL && Adapter->Mms2State == DxgkMms2AdapterAbsent)
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (Adapter->Mms2Adapter == NULL || (Adapter->Mms2State != DxgkMms2AdapterCreated && Adapter->Mms2State != DxgkMms2AdapterStopped))
    {
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Mms2Adapter = Adapter->Mms2Adapter;
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    DxgkpMms2ClearContextStreamInterface(Adapter);
    TimelineWasPublished = DxgkpCloseMms2TimelineCalls(Adapter);
    if (!TimelineWasPublished && InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0)
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    Status = DxgkpMms2DestroyAdapter(Mms2Adapter);
    if (!NT_SUCCESS(Status))
    {
        if (TimelineWasPublished)
            DxgkpReopenMms2TimelineCalls(Adapter);
        return Status;
    }
    InterlockedExchange(&Adapter->Mms2TimelineValid, 0);
    KeMemoryBarrier();
    RtlZeroMemory(&Adapter->Mms2Timeline, sizeof(Adapter->Mms2Timeline));
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->Mms2Adapter == Mms2Adapter)
    {
        Adapter->Mms2Adapter = NULL;
        Adapter->Mms2State = DxgkMms2AdapterAbsent;
        Adapter->Mms2StopReason = 0;
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpMms2StartAdministrativeAdapter(_In_ PDXGKRNL_ADAPTER Adapter, _Out_ PBOOLEAN ProviderStarted)
{
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 ContextStreamInterface;
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Timeline;
    ULONGLONG EnabledSubsystems;
    ULONG HighestCompleteWddmVersion;
    ULONG RequestedWddmVersion;
    BOOLEAN TimelineWasPublished;
    NTSTATUS Status;

    EnabledSubsystems = 0;
    HighestCompleteWddmVersion = 0;
    RequestedWddmVersion = min(Adapter->MiniportContext->InitData.s.Version, (ULONG)DXGKDDI_INTERFACE_VERSION);
    RtlZeroMemory(&ContextStreamInterface, sizeof(ContextStreamInterface));
    TimelineWasPublished = DxgkpCloseMms2TimelineCalls(Adapter);
    if (!TimelineWasPublished && InterlockedCompareExchange(&Adapter->Mms2TimelineValid, 0, 0) != 0)
        DxgkpBugCheckMms2Timeline(Adapter, 0, 0);
    Status = DxgkpMms2StartAdapter(Adapter->Mms2Adapter, Adapter->MiniportContext->InitData.s.Version, RequestedWddmVersion, Adapter->NodeCount, Adapter->SegmentCount, DxgkpMms2GetAdapterFlags(Adapter), Adapter->SchedulingCaps.Value, &EnabledSubsystems, &HighestCompleteWddmVersion, ProviderStarted);
    if (NT_SUCCESS(Status))
    {
        /* dxgmms2 reports which subsystems it actually owns.  The scheduler
         * bit is required: dxgkrnl has no run queue of its own to fall back
         * on, so a provider that does not own scheduling cannot drive it. */
        if ((EnabledSubsystems & (DXGMMS2_SUBSYSTEM_SCHEDULER | DXGMMS2_SUBSYSTEM_VIDMM)) != (DXGMMS2_SUBSYSTEM_SCHEDULER | DXGMMS2_SUBSYSTEM_VIDMM))
        {
            DXGKRNL_ERR("dxgmms2 does not own the scheduler and VidMm subsystems (0x%I64X)\n", EnabledSubsystems);
            Status = STATUS_REVISION_MISMATCH;
        }
        Adapter->Mms2EnabledSubsystems = EnabledSubsystems;
        Adapter->Mms2HighestCompleteWddmVersion = HighestCompleteWddmVersion;
        if (NT_SUCCESS(Status))
            Status = DxgkpMms2QueryVidMmInterface(Adapter->Mms2Adapter, &Adapter->Mms2VidMmInterface);
        if (NT_SUCCESS(Status))
        {
            InterlockedExchange(&Adapter->Mms2VidMmValid, 1);
            /* Segment geometry is discovered before the provider starts, so
             * it is published here, once the owner exists. */
            Status = DxgkVidMmPublishSegments(Adapter);
            if (!NT_SUCCESS(Status))
                InterlockedExchange(&Adapter->Mms2VidMmValid, 0);
        }
        if (NT_SUCCESS(Status))
            Status = DxgkpMms2QuerySchedulerInterface(Adapter->Mms2Adapter, &Adapter->Mms2SchedulerInterface);
        if (NT_SUCCESS(Status))
        {
            /* StartAdapter already started the provider-owned scheduler.
             * Publishing the queried interface does not start it a second
             * time. */
            InterlockedExchange(&Adapter->Mms2SchedulerValid, 1);
        }
        if (NT_SUCCESS(Status))
            Status = DxgkpMms2QuerySchedulerTimeline(Adapter->Mms2Adapter, &Timeline);
        if (NT_SUCCESS(Status) && Timeline.NodeCount != Adapter->NodeCount)
            Status = STATUS_REVISION_MISMATCH;
        if (NT_SUCCESS(Status))
            Status = DxgkpMms2QueryContextStreamInterface(Adapter->Mms2Adapter, &ContextStreamInterface);
        if (NT_SUCCESS(Status))
        {
            Adapter->Mms2Timeline = Timeline;
            DxgkpMms2PublishContextStreamInterface(Adapter, &ContextStreamInterface);
            DxgkpPublishMms2TimelineCalls(Adapter);
        }
    }
    if (!NT_SUCCESS(Status) && !*ProviderStarted && TimelineWasPublished)
        DxgkpReopenMms2TimelineCalls(Adapter);
    else if (!NT_SUCCESS(Status) && *ProviderStarted)
    {
        DxgkpMms2UnpublishContextStreamInterface(Adapter);
        InterlockedExchange(&Adapter->Mms2TimelineValid, 0);
        KeMemoryBarrier();
    }
    return Status;
}

static NTSTATUS
DxgkpBeginAdapterStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PULONG Generation)
{
    ULONG NextGeneration;
    NTSTATUS Status = STATUS_SUCCESS;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->State != DxgkAdapterStateStopped || InterlockedCompareExchange(&Adapter->AdapterStopInProgress, 0, 0) != 0 || Adapter->AdapterStopIntentCount != 0)
    {
        Status = STATUS_DEVICE_BUSY;
    }
    else
    {
        NextGeneration = Adapter->AdapterStartGeneration + 1;
        if (NextGeneration == 0)
            NextGeneration = 1;
        Adapter->AdapterStartGeneration = NextGeneration;
        Adapter->AdapterStartStatus = STATUS_PENDING;
        KeResetEvent(&Adapter->AdapterStartCompletedEvent);
        Adapter->State = DxgkAdapterStateStarting;
        *Generation = NextGeneration;
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    return Status;
}

static VOID
DxgkpCompleteAdapterStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Generation,
    _In_ NTSTATUS Status,
    _In_ BOOLEAN Restartable)
{
    BOOLEAN QueueHotPlug = FALSE;

    /*
     * A restartable failure has crossed a proven StopDevice boundary (or the
     * miniport never completed StartDevice), so it must not remain the boot
     * display owner.  A non-restartable rollback failed to stop the miniport;
     * retain ownership until RemoveDevice rather than letting a second
     * claimant race hardware that can still be scanning out.
     */
    if (!NT_SUCCESS(Status) && Restartable)
        DxgkpClearPostDisplayOwner(Adapter);
    if (NT_SUCCESS(Status))
        DxgkPnpBeginChildEnumerationEpoch(Adapter);

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Adapter->AdapterStartGeneration == Generation);
    ASSERT(Adapter->State == DxgkAdapterStateStarting);
    if (Adapter->AdapterStartGeneration == Generation && Adapter->State == DxgkAdapterStateStarting)
    {
        Adapter->AdapterStartStatus = Status;
        Adapter->AdapterStartCompletedGeneration = Generation;
        Adapter->State = NT_SUCCESS(Status) ? DxgkAdapterStateStarted : (Restartable ? DxgkAdapterStateStopped : DxgkAdapterStateStopping);
        QueueHotPlug = NT_SUCCESS(Status);
        KeSetEvent(&Adapter->AdapterStartCompletedEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    DxgkpCompletePostDisplayHandoff(Adapter, Status, Restartable);
    if (QueueHotPlug)
        (VOID)DxgkVidPnQueueHotPlugRebuild(Adapter);
}

typedef struct _DXGKP_ADAPTER_START_PROGRESS
{
    BOOLEAN MiniportStarted;
    BOOLEAN VidMmStarted;
    BOOLEAN PagingSystemContextCreated;
    BOOLEAN Mms2Started;
    BOOLEAN SchedulerStarted;
    BOOLEAN VidPnCreated;
    BOOLEAN PresentStarted;
    BOOLEAN RundownReinitialized;
    BOOLEAN TdrStarted;
    BOOLEAN VsyncEnabled;
    BOOLEAN InterfaceEnabled;
    BOOLEAN DisplayRegistered;
} DXGKP_ADAPTER_START_PROGRESS, *PDXGKP_ADAPTER_START_PROGRESS;

static VOID
DxgkpDestroyAdapterVidPn(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    D3DKMDT_HVIDPN VidPn;

    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    VidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
    Adapter->VidPn = NULL;
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    if (VidPn != NULL)
        DxgkVidPnDestroy(VidPn);
}

static NTSTATUS
DxgkpCloseAndRetireReverseCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (InterlockedCompareExchange(&Adapter->ReverseCallbackRundownStarted, 1, 0) != 0)
        return STATUS_DELETE_PENDING;
    ExWaitForRundownProtectionRelease(&Adapter->ReverseCallbackRundownRef);
    DxgkpReleaseMapMemory(Adapter);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    DxgkpReleaseCallbackMemory(Adapter);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    DxgkpReleasePhysicalMemoryObjects(Adapter);
#endif
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpRollbackAdapterStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_ADAPTER_START_PROGRESS Progress,
    _In_ NTSTATUS FailureStatus,
    _Out_ PBOOLEAN Restartable)
{
    NTSTATUS BeginStatus = STATUS_SUCCESS;
    NTSTATUS StopStatus = STATUS_SUCCESS;
    NTSTATUS CompleteStatus = STATUS_SUCCESS;
    NTSTATUS InterfaceStatus = STATUS_SUCCESS;
    NTSTATUS ResourceStatus = STATUS_SUCCESS;

    *Restartable = FALSE;
    InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
    DxgkPresentBeginStop(Adapter);
    InterlockedExchange(&Adapter->VidSchStopping, 1);
    if (Progress->RundownReinitialized)
        DxgkBeginAdapterRundown(Adapter);
    if (Progress->TdrStarted)
        DxgkpStopTdrWatchdog(Adapter);
    if (Progress->VsyncEnabled)
    {
        NTSTATUS VsyncStatus = DxgkpSetVsyncInterruptState(Adapter, DXGK_VSYNC_DISABLE_NO_PHASE);

        if (!NT_SUCCESS(VsyncStatus) && VsyncStatus != STATUS_NOT_SUPPORTED)
            DXGKRNL_WARN("DxgkAdapterStart: rollback could not disable vsync 0x%08lX\n", VsyncStatus);
    }
    if (Progress->DisplayRegistered)
        DxgkDisplayUnregister(Adapter);
    if (Progress->InterfaceEnabled && Adapter->DeviceInterfaceEnabled)
    {
        InterfaceStatus = IoSetDeviceInterfaceState(&Adapter->DeviceInterfaceName, FALSE);
        if (NT_SUCCESS(InterfaceStatus))
            Adapter->DeviceInterfaceEnabled = FALSE;
        else
            DXGKRNL_ERR("DxgkAdapterStart: rollback could not disable the adapter interface 0x%08lX\n", InterfaceStatus);
    }
    if (Progress->Mms2Started)
        BeginStatus = DxgkpMms2BeginStop(Adapter, Dxgmms2StopReasonStartRollback);
    VidSchPrepareForStop(Adapter);
    DxgkpDisablePeriodicInterruptHandoff(Adapter);
    DxgkpDisconnectAdapterInterrupt(Adapter);
    KeRemoveQueueDpc(&Adapter->DpcObject);
    KeFlushQueuedDpcs();
    DxgkpWaitForVidSchCallbacks(Adapter);
    if (Progress->PagingSystemContextCreated)
    {
        StopStatus = DxgkDestroyPagingSystemContext(Adapter);
        if (!NT_SUCCESS(StopStatus))
        {
            DXGKRNL_ERR("DxgkAdapterStart: rollback could not destroy the paging system context 0x%08lX\n",
                        StopStatus);
            return StopStatus;
        }
    }
    if (Progress->MiniportStarted)
        StopStatus = DxgkpStopMiniportForTeardown(Adapter);
    if (!NT_SUCCESS(StopStatus))
    {
        DXGKRNL_ERR("DxgkAdapterStart: rollback could not establish ownership boundary 0x%08lX; retaining state for RemoveDevice\n", StopStatus);
        return StopStatus;
    }

    ResourceStatus = DxgkpCloseAndRetireReverseCallbacks(Adapter);
    if (!NT_SUCCESS(BeginStatus))
    {
        DXGKRNL_ERR("DxgkAdapterStart: dxgmms2 rollback could not begin 0x%08lX; retaining state for RemoveDevice\n", BeginStatus);
        return BeginStatus;
    }
    if (Progress->Mms2Started)
        CompleteStatus = DxgkpMms2CompleteRetiredStop(Adapter);
    if (!NT_SUCCESS(ResourceStatus) || !NT_SUCCESS(CompleteStatus))
    {
        NTSTATUS RollbackStatus = !NT_SUCCESS(ResourceStatus) ? ResourceStatus : CompleteStatus;

        DXGKRNL_ERR("DxgkAdapterStart: rollback retirement incomplete 0x%08lX; retaining state for RemoveDevice\n", RollbackStatus);
        return RollbackStatus;
    }

    if (Progress->PresentStarted)
        DxgkPresentTeardown(Adapter);
    if (Progress->VidPnCreated)
        DxgkpDestroyAdapterVidPn(Adapter);
    if (Progress->SchedulerStarted)
        VidSchDestroy(Adapter);
    if (Progress->VidMmStarted)
    {
        DxgkpDrainDmaBufferCache(Adapter);
        DxgkVidMmQuiesceAdapter(Adapter);
        DxgkVidMmTeardownAdapter(Adapter);
    }
    DxgkpClearPostDisplayOwner(Adapter);
    DxgkpReleasePostDisplayMapping(Adapter);
    DxgkpReleaseAdapterResources(Adapter);
    Adapter->PresentQueueInitializationStatus = STATUS_DEVICE_NOT_READY;
    Adapter->InterruptTraceEpoch100ns = 0;
    Adapter->NodeCount = 0;
    Adapter->GpuMmuCapsValid = FALSE;
    Adapter->PageTableLevelsValid = FALSE;
    if (!NT_SUCCESS(InterfaceStatus))
        return InterfaceStatus;
    *Restartable = TRUE;
    return FailureStatus;
}

/* Returns with AdapterMutex held after every in-flight start has completed. */
static VOID
DxgkpAcquireAdapterMutexAfterStart(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    for (;;)
    {
        ULONG Generation;

        (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
        if (Adapter->State != DxgkAdapterStateStarting)
            return;
        Generation = Adapter->AdapterStartGeneration;
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        KeWaitForSingleObject(&Adapter->AdapterStartCompletedEvent, Executive, KernelMode, FALSE, NULL);
        (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
        if (Adapter->AdapterStartCompletedGeneration == Generation && Adapter->State != DxgkAdapterStateStarting)
            return;
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    }
}

static NTSTATUS
DxgkpCollectAdapterDiagnosticInfo(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_DIAGNOSTICINFO_TYPE Type,
    _In_ BOOLEAN AcquireCallbackGate)
{
    PDXGKDDI_COLLECTDIAGNOSTICINFO CollectDiagnosticInfo;
    DXGKARG_COLLECTDIAGNOSTICINFO Args;
    PVOID Buffer;
    NTSTATUS Status;
    BOOLEAN CallbackAcquired;

    PAGED_CODE();

    if (Adapter == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->PhysicalDeviceObject == NULL ||
        Adapter->MiniportContext->UseDodLayout ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_6))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Type == DXGK_DI_BLACKSCREEN &&
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_7))
    {
        return STATUS_NOT_SUPPORTED;
    }

    CollectDiagnosticInfo =
        DXGK_CB_FULL(Adapter, DxgkDdiCollectDiagnosticInfo);
    if (CollectDiagnosticInfo == NULL)
        return STATUS_NOT_SUPPORTED;

    Buffer = ExAllocatePoolWithTag(
                 PagedPool,
                 DXGKP_DIAGNOSTIC_BUFFER_SIZE,
                 TAG_DXGK_ADAPTER);
    if (Buffer != NULL)
        RtlZeroMemory(Buffer, DXGKP_DIAGNOSTIC_BUFFER_SIZE);

    RtlZeroMemory(&Args, sizeof(Args));
    Args.hAdapter = Adapter->MiniportDeviceContext;
    Args.Type = Type;
    Args.BufferSizeIn =
        Buffer != NULL ? DXGKP_DIAGNOSTIC_BUFFER_SIZE : 0;
    Args.pBuffer = Buffer;
    CallbackAcquired = FALSE;

    if (AcquireCallbackGate)
    {
        if (!DxgkAcquireMiniportCallback(Adapter))
        {
            Status = STATUS_DELETE_PENDING;
            goto Cleanup;
        }
        CallbackAcquired = TRUE;
    }

    _SEH2_TRY
    {
        Status = CollectDiagnosticInfo(
                     Adapter->PhysicalDeviceObject,
                     &Args);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (CallbackAcquired)
        DxgkReleaseMiniportCallback(Adapter);

    Args.BucketingString[DXGK_DUMP_BUCKETING_BUFFER_SIZE - 1] = '\0';
    Args.DescriptionString[DXGK_DUMP_DESCRIPTION_BUFFER_SIZE - 1] = '\0';
    if (Args.BufferSizeOut > Args.BufferSizeIn)
    {
        DXGKRNL_ERR("DxgkCollectAdapterDiagnosticInfo: miniport returned "
                    "oversized payload %u > %u\n",
                    Args.BufferSizeOut,
                    Args.BufferSizeIn);
        Args.BufferSizeOut = 0;
        Status = STATUS_DATA_ERROR;
    }

    DXGKRNL_WARN("DxgkCollectAdapterDiagnosticInfo: type=%u "
                 "status=0x%08lX bucket=%s description=%s bytes=%u\n",
                 (UINT)Type,
                 Status,
                 Args.BucketingString,
                 Args.DescriptionString,
                 Args.BufferSizeOut);

Cleanup:
    if (Buffer != NULL)
        ExFreePoolWithTag(Buffer, TAG_DXGK_ADAPTER);
    return Status;
}

NTSTATUS
DxgkCollectAdapterDiagnosticInfo(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_DIAGNOSTICINFO_TYPE Type)
{
    return DxgkpCollectAdapterDiagnosticInfo(Adapter, Type, TRUE);
}

static NTSTATUS
DxgkpSetVsyncInterruptState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_CRTC_VSYNC_STATE VsyncState)
{
    PDXGKDDI_CONTROLINTERRUPT3 ControlInterrupt3;
    PDXGKDDI_CONTROLINTERRUPT2 ControlInterrupt2;
    PDXGKDDI_CONTROL_INTERRUPT ControlInterrupt;
    DXGKARG_CONTROLINTERRUPT3 Args3;
    DXGKARG_CONTROLINTERRUPT2 Args2;
    ULONG Version;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;

    Version = Adapter->MiniportContext->InitData.s.Version;
    ControlInterrupt3 = NULL;
    ControlInterrupt2 = NULL;
    ControlInterrupt = DXGK_CB(Adapter, DxgkDdiControlInterrupt);

    if (!Adapter->MiniportContext->UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_7))
    {
        ControlInterrupt3 =
            DXGK_CB_FULL(Adapter, DxgkDdiControlInterrupt3);
    }
    if (ControlInterrupt3 == NULL &&
        !Adapter->MiniportContext->UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
    {
        ControlInterrupt2 =
            DXGK_CB_FULL(Adapter, DxgkDdiControlInterrupt2);
    }

    if (ControlInterrupt3 == NULL &&
        ControlInterrupt2 == NULL &&
        ControlInterrupt == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DELETE_PENDING;

    _SEH2_TRY
    {
        if (ControlInterrupt3 != NULL)
        {
            RtlZeroMemory(&Args3, sizeof(Args3));
            Args3.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
            Args3.CrtcVsyncState = VsyncState;
            Args3.VidPnSourceId = 0;
            Status = ControlInterrupt3(
                         Adapter->MiniportDeviceContext,
                         &Args3);
        }
        else if (ControlInterrupt2 != NULL)
        {
            RtlZeroMemory(&Args2, sizeof(Args2));
            Args2.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
            Args2.CrtcVsyncState = VsyncState;
            Status = ControlInterrupt2(
                         Adapter->MiniportDeviceContext,
                         Args2);
        }
        else
        {
            Status = ControlInterrupt(
                         Adapter->MiniportDeviceContext,
                         DXGK_INTERRUPT_CRTC_VSYNC,
                         VsyncState == DXGK_VSYNC_ENABLE);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    DxgkReleaseMiniportCallback(Adapter);
    /* This is the only place the vsync interrupt is turned on or off, so it
     * is the only place that can answer whether it is on. */
    if (NT_SUCCESS(Status))
    {
        InterlockedExchange(&Adapter->VsyncInterruptEnabled,
                            (VsyncState == DXGK_VSYNC_ENABLE) ? 1 : 0);
    }
    return Status;
}

/*
 * DxgkpEnableEngineInterrupt
 *
 * Turns on a non-vsync interrupt source.  dxgkrnl only ever enabled
 * DXGK_INTERRUPT_CRTC_VSYNC, so the engine's DMA-completion interrupt was
 * left off and the miniport reported completions from its own periodic check
 * instead -- measured at a fixed ~480 ms per submission, which throttles every
 * present to ~2 Hz and makes windows look hung.
 */
static NTSTATUS
DxgkpEnableEngineInterrupt(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGK_INTERRUPT_TYPE InterruptType)
{
    PDXGKDDI_CONTROLINTERRUPT3 ControlInterrupt3 = NULL;
    PDXGKDDI_CONTROLINTERRUPT2 ControlInterrupt2 = NULL;
    PDXGKDDI_CONTROL_INTERRUPT ControlInterrupt;
    DXGKARG_CONTROLINTERRUPT3 Args3;
    DXGKARG_CONTROLINTERRUPT2 Args2;
    ULONG Version;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;

    Version = Adapter->MiniportContext->InitData.s.Version;
    ControlInterrupt = DXGK_CB(Adapter, DxgkDdiControlInterrupt);
    if (!Adapter->MiniportContext->UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_7))
    {
        ControlInterrupt3 = DXGK_CB_FULL(Adapter, DxgkDdiControlInterrupt3);
    }
    if (ControlInterrupt3 == NULL && !Adapter->MiniportContext->UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
    {
        ControlInterrupt2 = DXGK_CB_FULL(Adapter, DxgkDdiControlInterrupt2);
    }
    if (ControlInterrupt3 == NULL && ControlInterrupt2 == NULL && ControlInterrupt == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DELETE_PENDING;

    _SEH2_TRY
    {
        if (ControlInterrupt3 != NULL)
        {
            RtlZeroMemory(&Args3, sizeof(Args3));
            Args3.InterruptType = InterruptType;
            Status = ControlInterrupt3(Adapter->MiniportDeviceContext, &Args3);
        }
        else if (ControlInterrupt2 != NULL)
        {
            RtlZeroMemory(&Args2, sizeof(Args2));
            Args2.InterruptType = InterruptType;
            Status = ControlInterrupt2(Adapter->MiniportDeviceContext, Args2);
        }
        else
        {
            Status = ControlInterrupt(Adapter->MiniportDeviceContext,
                                      InterruptType, TRUE);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseMiniportCallback(Adapter);
    return Status;
}

/*
 * DxgkAdapterStart
 *
 * Called from DxgkpMiniportPnpDispatch in response to IRP_MN_START_DEVICE.
 * Captures the resource lists, fills the DXGK_INTERFACE, and calls
 * DxgkDdiStartDevice on the miniport.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkAdapterStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PCM_RESOURCE_LIST AllocatedResources,
    _In_ PCM_RESOURCE_LIST TranslatedResources)
{
    DXGK_START_INFO StartInfo;
    DXGK_INTERFACE  Interface;
    NTSTATUS        Status;
    ULONGLONG       AdapterStart100ns;
    ULONGLONG       StepStart100ns;
    ULONGLONG       InterruptConnectUs = 0;
    ULONGLONG       MiniportStartUs = 0;
    ULONGLONG       VidMmUs = 0;
    ULONGLONG       VidPnUs = 0;
    ULONGLONG       PresentUs = 0;
    ULONGLONG       DisplayUs = 0;
    ULONG           StartGeneration;
    DXGK_ADAPTER_START_ROLE Role;
    DXGKP_ADAPTER_START_PROGRESS Progress;
    BOOLEAN         Restartable;

    PAGED_CODE();

    RtlZeroMemory(&Progress, sizeof(Progress));
    AdapterStart100ns = DxgkpTraceNow100ns();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);
    if (Adapter->MiniportRemoveDeviceComplete ||
        Adapter->MiniportDeviceContext == NULL ||
        InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
    {
        DXGKRNL_ERR("DxgkAdapterStart: miniport device has crossed its final "
                    "RemoveDevice boundary; refusing restart\n");
        return STATUS_DELETE_PENDING;
    }
    Status = DxgkpBeginAdapterStart(Adapter, &StartGeneration);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DxgkpCaptureAdapterResources(Adapter, AllocatedResources, TranslatedResources);
    if (!NT_SUCCESS(Status))
    {
        DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, TRUE);
        return Status;
    }
    if (InterlockedCompareExchange(&Adapter->ReverseCallbackRundownStarted, 0, 0) != 0)
    {
        ExReInitializeRundownProtection(&Adapter->ReverseCallbackRundownRef);
        InterlockedExchange(&Adapter->ReverseCallbackRundownStarted, 0);
    }
    AllocatedResources = Adapter->AllocatedResources;
    TranslatedResources = Adapter->TranslatedResources;
    Adapter->MiniportDeviceStopped = FALSE;
    Adapter->SurpriseRemovalHandled = FALSE;
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);

    Adapter->InterruptCount = 0;
    Adapter->QueueDpcCount = 0;
    Adapter->DpcCount = 0;
    RtlZeroMemory((PVOID)Adapter->NotifyInterruptTypeCount,
                  sizeof(Adapter->NotifyInterruptTypeCount));
    Adapter->LastDmaCompletedFence = 0;
    Adapter->LastDmaCompletedCounter = 0;
    Adapter->LastMiniportDpcCompletedCounter = 0;
    Adapter->LastPageFaultFence = 0;
    Adapter->LastPageFaultPrimitiveSequence = 0;
    Adapter->LastPageFaultPipelineStage = 0;
    Adapter->LastPageFaultBindTableEntry = 0;
    Adapter->LastPageFaultFlags = 0;
    Adapter->LastPageFaultVirtualAddress = 0;
    Adapter->LastPageFaultNode = 0;
    Adapter->LastPageFaultEngine = 0;
    Adapter->LastPageFaultLevel = 0;
    Adapter->LastPageFaultErrorCode = 0;
    Adapter->LastPageFaultProcessHandle = 0;
    Adapter->InterruptTraceEpoch100ns = AdapterStart100ns;
    Adapter->InterruptMessageBased = FALSE;
    Adapter->InterruptMessageCount = 0;
    Adapter->InterruptVector = 0;
    Adapter->InterruptLevel = PASSIVE_LEVEL;
    Adapter->InterruptAffinity = 0;
    Adapter->InterruptShared = FALSE;
    Adapter->InterruptMode = LevelSensitive;

    DXGKRNL_TRACE("DxgkAdapterStart: Adapter %p AllocRes=%p TransRes=%p\n",
                  Adapter, AllocatedResources, TranslatedResources);

    if (TranslatedResources && TranslatedResources->Count > 0)
    {
        ULONG i;
        PCM_PARTIAL_RESOURCE_LIST PartialList = &TranslatedResources->List[0].PartialResourceList;
        DXGKRNL_TRACE("DxgkAdapterStart: %lu translated resources\n", PartialList->Count);
        for (i = 0; i < PartialList->Count; i++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &PartialList->PartialDescriptors[i];
            DXGKRNL_TRACE("  [%lu] Type=%u Start=0x%llX Length=0x%lX\n",
                          i, Desc->Type,
                          (ULONGLONG)Desc->u.Memory.Start.QuadPart,
                          Desc->u.Memory.Length);
        }
    }
    else
    {
        /* Normal for root-enumerated software adapters (softgpu); miniports
         * that require hardware resources fail their own StartDevice. */
        DXGKRNL_TRACE("DxgkAdapterStart: no translated resources (PDO=%p DOD=%d)\n",
                      Adapter->PhysicalDeviceObject,
                      Adapter->MiniportContext ? Adapter->MiniportContext->IsDisplayOnlyDriver : -1);
    }

    /* Save interrupt resource info for connection immediately before
     * DxgkDdiStartDevice, which may require initialization completions. */
    if (TranslatedResources && TranslatedResources->Count > 0)
    {
        ULONG ri;
        PCM_PARTIAL_RESOURCE_LIST PartialList = &TranslatedResources->List[0].PartialResourceList;
        for (ri = 0; ri < PartialList->Count; ri++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &PartialList->PartialDescriptors[ri];
            if (Desc->Type == CmResourceTypeInterrupt)
            {
                /* The assigned translated descriptor is authoritative.  Loss
                 * of the MESSAGE flag is a PCI/PnP translation defect and must
                 * not be guessed around from live configuration registers. */
                Adapter->InterruptMessageBased =
                    (Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
                    ? TRUE : FALSE;

                if (Adapter->InterruptMessageBased)
                {
                    Adapter->InterruptVector =
                        Desc->u.MessageInterrupt.Translated.Vector;
                    Adapter->InterruptLevel =
                        (KIRQL)Desc->u.MessageInterrupt.Translated.Level;
                    Adapter->InterruptAffinity =
                        Desc->u.MessageInterrupt.Translated.Affinity;
                    Adapter->InterruptShared = FALSE;
                    Adapter->InterruptMode = Latched;
                    Adapter->InterruptMessageCount = 1;

                    DXGKRNL_TRACE("DxgkAdapterStart: saved MSI interrupt — "
                                  "BaseVector=%lu Count=%lu IRQL=%u\n",
                                  Adapter->InterruptVector,
                                  Adapter->InterruptMessageCount,
                                  Adapter->InterruptLevel);
                }
                else
                {
                    Adapter->InterruptVector   = Desc->u.Interrupt.Vector;
                    Adapter->InterruptLevel    = (KIRQL)Desc->u.Interrupt.Level;
                    Adapter->InterruptAffinity = Desc->u.Interrupt.Affinity;
                    Adapter->InterruptShared   =
                        (Desc->ShareDisposition == CmResourceShareShared);
                    Adapter->InterruptMode     =
                        (Desc->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                        Latched : LevelSensitive;

                    DXGKRNL_TRACE("DxgkAdapterStart: saved interrupt — Vector=%lu IRQL=%u\n",
                                  Adapter->InterruptVector,
                                  Adapter->InterruptLevel);
                }
                break;
            }
        }
    }

    /* Fill the callback table for the miniport.  DxgkpFillInterface zeros the
     * full current buffer before publishing the version-specific prefix.
     * Unsupported published slots are explicit failure/suppression callbacks;
     * their dependent capabilities remain disabled. */
    DxgkpFillInterface(Adapter, &Interface);

    /* Build the start-info block. */
    RtlZeroMemory(&StartInfo, sizeof(StartInfo));
    StartInfo.RequiredDmaQueueEntry = 32;
    StartInfo.AdapterGuid = Adapter->AdapterGuid;
    StartInfo.AdapterLuid = Adapter->AdapterLuid;

    DXGKRNL_TRACE("DxgkAdapterStart: identity GUID={%08lX-%04X-%04X-"
                  "%02X%02X-%02X%02X%02X%02X%02X%02X} LUID=%08lX:%08lX\n",
                  StartInfo.AdapterGuid.Data1,
                  StartInfo.AdapterGuid.Data2,
                  StartInfo.AdapterGuid.Data3,
                  StartInfo.AdapterGuid.Data4[0],
                  StartInfo.AdapterGuid.Data4[1],
                  StartInfo.AdapterGuid.Data4[2],
                  StartInfo.AdapterGuid.Data4[3],
                  StartInfo.AdapterGuid.Data4[4],
                  StartInfo.AdapterGuid.Data4[5],
                  StartInfo.AdapterGuid.Data4[6],
                  StartInfo.AdapterGuid.Data4[7],
                  StartInfo.AdapterLuid.HighPart,
                  StartInfo.AdapterLuid.LowPart);

    /* Connect the interrupt before StartDevice.  The miniport is required to
     * enable its hardware interrupts from StartDevice and can wait for an
     * interrupt-driven initialization completion before returning. */
    DxgkAcquireLevel3Transition(Adapter);
    DxgkBeginKmdExclusive(Adapter);
    InterlockedExchange(&Adapter->VidSchStopping, 1);
    DxgkpWaitForVidSchCallbacks(Adapter);
    InterlockedExchange(&Adapter->VidSchStopping, 0);
    if (Adapter->InterruptVector != 0 &&
        Adapter->MiniportContext->InitData.s.DxgkDdiInterruptRoutine != NULL)
    {
        IO_CONNECT_INTERRUPT_PARAMETERS ConnectParams;
        RtlZeroMemory(&ConnectParams, sizeof(ConnectParams));

        if (Adapter->InterruptMessageBased)
        {
            ConnectParams.Version = CONNECT_MESSAGE_BASED;
            ConnectParams.MessageBased.PhysicalDeviceObject =
                Adapter->PhysicalDeviceObject;
            ConnectParams.MessageBased.ConnectionContext.InterruptMessageTable =
                &Adapter->InterruptMessageTable;
            ConnectParams.MessageBased.MessageServiceRoutine =
                DxgkpMessageIsrTrampoline;
            ConnectParams.MessageBased.ServiceContext = Adapter;
            ConnectParams.MessageBased.SpinLock = NULL;
            ConnectParams.MessageBased.SynchronizeIrql =
                Adapter->InterruptLevel;
            ConnectParams.MessageBased.FloatingSave = FALSE;
            ConnectParams.MessageBased.FallBackServiceRoutine = NULL;
        }
        else
        {
            ConnectParams.Version = CONNECT_FULLY_SPECIFIED;
            ConnectParams.FullySpecified.PhysicalDeviceObject =
                Adapter->PhysicalDeviceObject;
            ConnectParams.FullySpecified.InterruptObject =
                &Adapter->InterruptObject;
            ConnectParams.FullySpecified.ServiceRoutine = DxgkpIsrTrampoline;
            ConnectParams.FullySpecified.ServiceContext = Adapter;
            ConnectParams.FullySpecified.SpinLock = NULL;
            ConnectParams.FullySpecified.SynchronizeIrql =
                Adapter->InterruptLevel;
            ConnectParams.FullySpecified.FloatingSave = FALSE;
            ConnectParams.FullySpecified.ShareVector = Adapter->InterruptShared;
            ConnectParams.FullySpecified.Vector = Adapter->InterruptVector;
            ConnectParams.FullySpecified.Irql = Adapter->InterruptLevel;
            ConnectParams.FullySpecified.InterruptMode = Adapter->InterruptMode;
            ConnectParams.FullySpecified.ProcessorEnableMask =
                Adapter->InterruptAffinity;
        }

        StepStart100ns = DxgkpTraceNow100ns();
        Status = IoConnectInterruptEx(&ConnectParams);
        InterruptConnectUs = DxgkpTraceElapsedUs(StepStart100ns);
        if (NT_SUCCESS(Status))
        {
            if (Adapter->InterruptMessageTable != NULL &&
                Adapter->InterruptMessageTable->MessageCount > 0)
            {
                Adapter->InterruptObject =
                    Adapter->InterruptMessageTable->MessageInfo[0].InterruptObject;
                Adapter->InterruptMessageCount =
                    Adapter->InterruptMessageTable->MessageCount;
            }

            DXGKRNL_TRACE("DxgkAdapterStart: Interrupt connected pre-start — "
                          "Vector=%lu MessageBased=%d Count=%lu\n",
                          Adapter->InterruptVector,
                          Adapter->InterruptMessageBased,
                          Adapter->InterruptMessageTable ?
                              Adapter->InterruptMessageTable->MessageCount : 1);

            /* The connection is now a valid synchronization boundary.  Open
             * ISR/DPC admission before calling StartDevice so a miniport can
             * complete initialization work through its interrupt routine.
             * Failure teardown closes and drains this gate before disconnect. */
            DxgkUnblockInterruptCallbacks(Adapter);
        }
        else
        {
            DXGKRNL_ERR("DxgkAdapterStart: mandatory IoConnectInterruptEx failed 0x%08lX\n", Status);
            InterlockedExchange(&Adapter->VidSchStopping, 1);
            DxgkpDisconnectAdapterInterrupt(Adapter);
            KeRemoveQueueDpc(&Adapter->DpcObject);
            KeFlushQueuedDpcs();
            DxgkpWaitForVidSchCallbacks(Adapter);
            DxgkpReleasePostDisplayMapping(Adapter);
            Restartable = NT_SUCCESS(DxgkpCloseAndRetireReverseCallbacks(Adapter));
            if (!Restartable)
                Status = STATUS_DELETE_PENDING;
            DxgkpReleaseAdapterResources(Adapter);
            Adapter->InterruptTraceEpoch100ns = 0;
            DxgkEndKmdExclusive(Adapter, FALSE);
            DxgkReleaseLevel3Transition(Adapter);
            DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, Restartable);
            return Status;
        }
    }

    /* Call miniport start. */
    DXGKRNL_TRACE("DxgkAdapterStart: calling DxgkDdiStartDevice MiniportCtx=%p\n",
                  Adapter->MiniportDeviceContext);
    StepStart100ns = DxgkpTraceNow100ns();
    Status = Adapter->MiniportContext->InitData.s.DxgkDdiStartDevice(
                 Adapter->MiniportDeviceContext,
                 &StartInfo,
                 &Interface,
                 &Adapter->NumberOfVideoPresentSources,
                 &Adapter->NumberOfChildren);
    MiniportStartUs = DxgkpTraceElapsedUs(StepStart100ns);
    DXGKRNL_TRACE("DxgkAdapterStart: DxgkDdiStartDevice returned 0x%08lX after %I64u us\n",
                  Status,
                  MiniportStartUs);

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStart: DxgkDdiStartDevice failed 0x%08lX (IRQs fired during start=%ld, vec=%lu msgbased=%d, mappings taken=%ld)\n",
                    Status, Adapter->InterruptCount,
                    Adapter->InterruptVector, Adapter->InterruptMessageBased,
                    Adapter->MapMemoryCallCount);
        /* Do not call another miniport DDI from this failure path.  StartDevice
         * can nevertheless leave MiniportDeviceContext-owned objects alive
         * for DxgkDdiRemoveDevice, and those objects may still own allocations
         * obtained through the reverse callbacks.  Disconnect OS producers
         * and roll back display ownership here, but keep the reverse-callback
         * rundown and its tracked allocations alive through RemoveDevice. */
        InterlockedExchange(&Adapter->VidSchStopping, 1);
        DxgkpDisconnectAdapterInterrupt(Adapter);
        KeRemoveQueueDpc(&Adapter->DpcObject);
        KeFlushQueuedDpcs();
        DxgkpWaitForVidSchCallbacks(Adapter);
        /*
         * A miniport can acquire the firmware display and then reject the
         * descriptor or fail a later allocation before StartDevice returns.
         * Do not leave that failed adapter published as the POST owner: doing
         * so would make a fallback or a restart yield forever.
         */
        DxgkpClearPostDisplayOwner(Adapter);
        DxgkpReleasePostDisplayMapping(Adapter);
        DxgkpReleaseAdapterResources(Adapter);
        DXGKRNL_TRACE("DxgkAdapterStart: fail summary connect=%I64u us miniport=%I64u us total=%I64u us irq=%ld queue=%ld dpc=%ld\n",
                      InterruptConnectUs,
                      MiniportStartUs,
                      DxgkpTraceElapsedUs(AdapterStart100ns),
                      Adapter->InterruptCount,
                      Adapter->QueueDpcCount,
                      Adapter->DpcCount);
        Adapter->InterruptTraceEpoch100ns = 0;
        DxgkEndKmdExclusive(Adapter, FALSE);
        DxgkReleaseLevel3Transition(Adapter);
        DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, TRUE);
        return Status;
    }

    Progress.MiniportStarted = TRUE;

    DxgkpEnablePeriodicInterruptHandoff(Adapter);
    DxgkEndKmdExclusive(Adapter, TRUE);
    DxgkUnblockInterruptCallbacks(Adapter);
    DxgkReleaseLevel3Transition(Adapter);

    DXGKRNL_TRACE("DxgkAdapterStart: started — Sources=%lu Children=%lu\n",
                  Adapter->NumberOfVideoPresentSources,
                  Adapter->NumberOfChildren);

    /* Initialise the video memory manager for this adapter. */
    StepStart100ns = DxgkpTraceNow100ns();
    Status = DxgkVidMmInitializeAdapter(Adapter);
    VidMmUs = DxgkpTraceElapsedUs(StepStart100ns);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStart: DxgkVidMmInitializeAdapter failed "
                    "0x%08lX\n", Status);
        goto StartRollback;
    }
    Progress.VidMmStarted = TRUE;
    /* A previous stop drained every segment-backed DMA buffer before tearing
     * VidMm down. Reopen the pool only after the new VidMm lifetime begins. */
    ASSERT(Adapter->DmaBufferCacheCount == 0);
    ASSERT(Adapter->DmaBufferCacheBytes == 0);
    InterlockedExchange(&Adapter->DmaBufferCacheStopping, 0);

    /* Cache surprise-removal support while hardware is still present.  The
     * topology fields apply only to full WDDM adapters. */
    Adapter->NodeCount = 0;
    Adapter->SupportSurpriseRemoval = FALSE;
    Adapter->ApertureSegmentCommitLimit = 0;

    /*
     * Node accounting starts from zero on every start, and the clock it is
     * measured with is captured here so the DISPATCH_LEVEL charge path never
     * asks for the frequency and every reader divides by the same number.
     */
    RtlZeroMemory(Adapter->NodeStatistics, sizeof(Adapter->NodeStatistics));
    RtlZeroMemory(Adapter->SystemNodeStatistics, sizeof(Adapter->SystemNodeStatistics));
    {
        LARGE_INTEGER PerformanceFrequency;
        ULONG NodeIndex;

        for (NodeIndex = 0; NodeIndex < RTL_NUMBER_OF(Adapter->NodeStatisticsLock); ++NodeIndex)
            KeInitializeSpinLock(&Adapter->NodeStatisticsLock[NodeIndex]);
        (VOID)KeQueryPerformanceCounter(&PerformanceFrequency);
        Adapter->PerformanceFrequency = PerformanceFrequency.QuadPart;
    }
    Role = DxgkAdapterStartClassifyRole(Adapter->MiniportContext->IsDisplayOnlyDriver, Adapter->NumberOfVideoPresentSources);
    {
        PDXGK_DRIVERCAPS Caps;
        NTSTATUS CapsStatus = STATUS_SUCCESS;

        Caps = ExAllocatePoolWithTag(NonPagedPool, DXGKP_DRIVERCAPS_QUERY_SIZE, TAG_DXGK_ADAPTER);
        if (Caps != NULL)
        {
            CapsStatus = DxgkpQueryDriverCaps(Adapter, Caps);
            if (NT_SUCCESS(CapsStatus))
            {
                Adapter->ApertureSegmentCommitLimit = Caps->ApertureSegmentCommitLimit;
                if (DxgkCapsCoreInterfaceVersionAtLeast(Adapter->MiniportContext->InitData.s.Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
                    Adapter->SupportSurpriseRemoval = Caps->SupportSurpriseRemoval;
                if (!Adapter->MiniportContext->IsDisplayOnlyDriver)
                {
                    /* The topology count is defined only for multi-engine miniports. */
                    Adapter->NodeCount = Caps->SchedulingCaps.MultiEngineAware ? Caps->GpuEngineTopology.NbAsymetricProcessingNodes : 1;
                    Adapter->HighestAcceptableAddress = Caps->HighestAcceptableAddress;
                    DXGKRNL_INFO("DxgkAdapterStart: driver caps: HighestAcceptableAddress=0x%I64x nodes=%lu scheduling=0x%08x\n",
                                (ULONGLONG)Caps->HighestAcceptableAddress.QuadPart, Adapter->NodeCount, Caps->SchedulingCaps.Value);
                    Adapter->SchedulingCaps.Value = Caps->SchedulingCaps.Value;
                    DXGKRNL_TRACE("DxgkAdapterStart: %lu GPU node(s) reported\n", Adapter->NodeCount);
                }
            }

            ExFreePoolWithTag(Caps, TAG_DXGK_ADAPTER);
        }
        else
        {
            CapsStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
        if (!Adapter->MiniportContext->IsDisplayOnlyDriver && !NT_SUCCESS(CapsStatus))
        {
            Status = CapsStatus;
            DXGKRNL_ERR("DxgkAdapterStart: mandatory full-adapter capability query failed 0x%08lX\n", Status);
            goto StartRollback;
        }
    }
    if (!DxgkAdapterStartRoleHasValidCounts(Role, Adapter->NumberOfVideoPresentSources, Adapter->NodeCount, DXGK_MAX_TRACKED_NODES))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        DXGKRNL_ERR("DxgkAdapterStart: invalid %s topology (sources=%lu nodes=%lu supported-nodes=%lu)\n", Role == DxgkAdapterStartDisplayOnly ? "DOD" : Role == DxgkAdapterStartRenderOnly ? "render-only" : "full-display", Adapter->NumberOfVideoPresentSources, Adapter->NodeCount, (ULONG)DXGK_MAX_TRACKED_NODES);
        goto StartRollback;
    }

    /* Cache the GPU MMU declaration while the miniport is callable. */
    Adapter->GpuMmuCapsValid = FALSE;
    Adapter->PageTableLevelsValid = FALSE;
    RtlZeroMemory(&Adapter->GpuMmuCaps, sizeof(Adapter->GpuMmuCaps));
    RtlZeroMemory(Adapter->PageTableLevels, sizeof(Adapter->PageTableLevels));
    if (DXGKP_GPUMMU_END_TO_END &&
        !Adapter->MiniportContext->IsDisplayOnlyDriver &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
    {
        NTSTATUS MmuStatus;
        BOOLEAN GeometryCached = FALSE;

        MmuStatus = DxgkpQueryGpuMmuCaps(Adapter, &Adapter->GpuMmuCaps);
        if (NT_SUCCESS(MmuStatus) &&
            Adapter->GpuMmuCaps.VirtualAddressBitCount != 0 &&
            Adapter->GpuMmuCaps.PageTableLevelCount != 0)
        {
            GeometryCached = DxgkpCacheGpuMmuGeometry(Adapter);
        }
        if (!GeometryCached)
        {
            /* Without the geometry no GPU virtual address space exists for
             * this adapter, so every reservation its miniport later asks
             * for is refused; say so here rather than at that point. */
            DXGKRNL_WARN("DxgkAdapterStart: GpuMmu caps unavailable for adapter %p: "
                         "status=0x%08lx va-bits=%u levels=%u update-mode=%u geometry=%u\n",
                         Adapter,
                         MmuStatus,
                         Adapter->GpuMmuCaps.VirtualAddressBitCount,
                         Adapter->GpuMmuCaps.PageTableLevelCount,
                         (UINT)Adapter->GpuMmuCaps.PageTableUpdateMode,
                         GeometryCached);
        }
    }
    DXGKRNL_INFO("DxgkAdapterStart: adapter %p LUID=%08lx-%08lx service=%wZ sources=%lu %s\n",
                 Adapter,
                 Adapter->AdapterLuid.HighPart,
                 Adapter->AdapterLuid.LowPart,
                 &Adapter->MiniportContext->RegistryPath,
                 Adapter->NumberOfVideoPresentSources,
                 Adapter->MiniportContext->IsBasicDisplayFallback ? "(basic display fallback)" :
                 Adapter->MiniportContext->IsDisplayOnlyDriver ? "(display only)" : "(full)");
    if (!Adapter->MiniportContext->IsDisplayOnlyDriver &&
        DXGK_CB_FULL(Adapter, DxgkDdiQueryAdapterInfo) != NULL)
    {
        /*
         * Register the miniport's runtime power components with the Power
         * Framework and let it start managing them.  dxgkrnl never picks an
         * F-state itself: every transition arrives from PoFx, which is what
         * makes DxgkCbSetPowerComponentActive/Idle meaningful to the driver.
         */
        NTSTATUS PowerStatus = DxgkpInitializePowerManagement(Adapter);

        if (NT_SUCCESS(PowerStatus))
            DxgkpStartRuntimePowerManagement(Adapter);
        else
        {
            DXGKRNL_WARN("DxgkAdapterStart: runtime power management unavailable 0x%08lX\n",
                         PowerStatus);
        }
    }
    if (!Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        /* Enable every non-vsync source the miniport will accept.  dxgkrnl
         * historically enabled only DXGK_INTERRUPT_CRTC_VSYNC, so any engine
         * source the driver needs was left masked.  Unsupported types answer
         * STATUS_NOT_SUPPORTED, which is not an error.  Vsync is owned by
         * DxgkpSetVsyncInterruptState and is deliberately not touched here. */
        ULONG InterruptType;

        for (InterruptType = 1; InterruptType <= 20; ++InterruptType)
        {
            if (InterruptType == (ULONG)DXGK_INTERRUPT_CRTC_VSYNC)
                continue;
            (VOID)DxgkpEnableEngineInterrupt(Adapter, (DXGK_INTERRUPT_TYPE)InterruptType);
        }
    }
    if (Adapter->PageTableLevelsValid)
    {
        Adapter->GpuMmuCapsValid = TRUE;
        DXGKRNL_INFO("DxgkAdapterStart: GpuMmu caps: va-bits=%u levels=%u update-mode=%u flags=0x%08x "
                    "(ReadOnly=%u NoExecute=%u ZeroInPte=%u ExplicitPTInvalidation=%u CacheCoherent=%u "
                    "RequireAddressSpaceIdle=%u LargePage=%u DualPte=%u InvalidTlbNotCached=%u CachedPageTables=%u) "
                    "MultiEngineAware=%u nodes=%lu\n",
                    Adapter->GpuMmuCaps.VirtualAddressBitCount,
                    Adapter->GpuMmuCaps.PageTableLevelCount,
                    (UINT)Adapter->GpuMmuCaps.PageTableUpdateMode,
                    Adapter->GpuMmuCaps.Value,
                    Adapter->GpuMmuCaps.ReadOnlyMemorySupported,
                    Adapter->GpuMmuCaps.NoExecuteMemorySupported,
                    Adapter->GpuMmuCaps.ZeroInPteSupported,
                    Adapter->GpuMmuCaps.ExplicitPageTableInvalidation,
                    Adapter->GpuMmuCaps.CacheCoherentMemorySupported,
                    Adapter->GpuMmuCaps.PageTableUpdateRequireAddressSpaceIdle,
                    Adapter->GpuMmuCaps.LargePageSupported,
                    Adapter->GpuMmuCaps.DualPteSupported,
                    Adapter->GpuMmuCaps.InvalidTlbEntriesNotCached,
                    Adapter->GpuMmuCaps.CachedPageTables,
                    (UINT)Adapter->SchedulingCaps.MultiEngineAware,
                    Adapter->NodeCount);
        DXGKRNL_TRACE("DxgkAdapterStart: GpuMmu %u-bit, %u level(s), leaf %u entry bits\n",
                      Adapter->GpuMmuCaps.VirtualAddressBitCount,
                      Adapter->GpuMmuCaps.PageTableLevelCount,
                      Adapter->PageTableLevels[0].PageTableIndexBitCount);
    }

    Adapter->PhysicalAdapterCapsValid = FALSE;
    RtlZeroMemory(&Adapter->PhysicalAdapterCaps, sizeof(Adapter->PhysicalAdapterCaps));
    if (!Adapter->MiniportContext->IsDisplayOnlyDriver &&
        DxgkCapsCoreInterfaceVersionAtLeast(Adapter->MiniportContext->InitData.s.Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0) &&
        DXGK_CB_FULL(Adapter, DxgkDdiQueryAdapterInfo) != NULL &&
        DxgkAcquireKmdCall(Adapter))
    {
        DXGKARG_QUERYADAPTERINFO QueryArgs;
        DXGK_QUERYPHYSICALADAPTERCAPSIN CapsIn;
        NTSTATUS CapsStatus;

        RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
        RtlZeroMemory(&CapsIn, sizeof(CapsIn));
        QueryArgs.Type = DXGKQAITYPE_PHYSICALADAPTERCAPS;
        QueryArgs.pInputData = &CapsIn;
        QueryArgs.InputDataSize = sizeof(CapsIn);
        QueryArgs.pOutputData = &Adapter->PhysicalAdapterCaps;
        QueryArgs.OutputDataSize = sizeof(Adapter->PhysicalAdapterCaps);
        _SEH2_TRY
        {
            CapsStatus = DXGK_CB_FULL(Adapter, DxgkDdiQueryAdapterInfo)(Adapter->MiniportDeviceContext, &QueryArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            CapsStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);
        Adapter->PhysicalAdapterCapsValid = NT_SUCCESS(CapsStatus);
        DXGKRNL_INFO("DxgkAdapterStart: physical adapter caps status=0x%08lx nodes=%u paging-node=%u handle=%p flags=0x%08x\n",
                    CapsStatus, Adapter->PhysicalAdapterCaps.NumExecutionNodes, Adapter->PhysicalAdapterCaps.PagingNodeIndex,
                    Adapter->PhysicalAdapterCaps.DxgkPhysicalAdapterHandle, Adapter->PhysicalAdapterCaps.Flags.Value);
    }
    /* Name every node's engine once: which node is the copy engine decides
     * where paging belongs when the physical adapter caps are refused. */
    if (DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata) != NULL && DxgkAcquireKmdCall(Adapter))
    {
        ULONG NodeOrdinal;

        for (NodeOrdinal = 0; NodeOrdinal < Adapter->NodeCount && NodeOrdinal < 16; NodeOrdinal++)
        {
            DXGK_NODEMETADATA Metadata;
            NTSTATUS MetaStatus;

            RtlZeroMemory(&Metadata, sizeof(Metadata));
            MetaStatus = DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata)(Adapter->MiniportDeviceContext, NodeOrdinal, &Metadata);
            DXGKRNL_INFO("DxgkAdapterStart: node %lu metadata status=0x%08lx engine-type=%u gpummu=%u iommu=%u\n",
                         NodeOrdinal, MetaStatus, (UINT)Metadata.EngineType, (UINT)Metadata.GpuMmuSupported, (UINT)Metadata.IoMmuSupported);
        }
        DxgkReleaseKmdCall(Adapter);
    }
    DxgkVidMmDumpSegments(Adapter);
    {
        BOOLEAN ProviderStarted;

        ProviderStarted = FALSE;
        Status = DxgkpMms2StartAdministrativeAdapter(Adapter, &ProviderStarted);
        if (ProviderStarted)
        {
            DxgkpMms2PublishStarted(Adapter);
            Progress.Mms2Started = TRUE;
        }
        if (!NT_SUCCESS(Status) || !ProviderStarted)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_INVALID_DEVICE_STATE;
            DXGKRNL_ERR("DxgkAdapterStart: dxgmms2 start failed 0x%08lX\n", Status);
            goto StartRollback;
        }
    }

    /* Full-display and render-only adapters require a live scheduler. */
    if (DxgkAdapterStartRoleRequiresScheduler(Role))
    {
        Status = VidSchInitialize(Adapter);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkAdapterStart: mandatory VidSchInitialize failed 0x%08lX\n", Status);
            goto StartRollback;
        }
        Progress.SchedulerStarted = TRUE;

        /* Native VidMmInitializePagingProcess creates its paging process and
         * system devices only after VidSch has established the scheduler
         * objects and port-lock state used by the miniport create callbacks. */
        Status = DxgkCreatePagingSystemContext(Adapter);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkAdapterStart: paging system context creation failed 0x%08lX\n",
                        Status);
            goto StartRollback;
        }
        Progress.PagingSystemContextCreated = TRUE;
    }

    /*
     * Create the VidPN (Video Present Network) for this adapter.
     * The VidPN is needed by the miniport driver for mode enumeration
     * (IsSupportedVidPn, EnumVidPnCofuncModality, CommitVidPn).
     */
    if (DxgkAdapterStartRoleRequiresDisplayPipeline(Role))
    {
        D3DKMDT_HVIDPN hVidPn = NULL;

        StepStart100ns = DxgkpTraceNow100ns();
        Status = DxgkVidPnCreateForAdapter(Adapter, &hVidPn);
        VidPnUs = DxgkpTraceElapsedUs(StepStart100ns);
        if (NT_SUCCESS(Status))
        {
            (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
            Adapter->VidPn = (PVOID)hVidPn;
            KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
            Progress.VidPnCreated = TRUE;
            DXGKRNL_TRACE("DxgkAdapterStart: VidPN created %p\n", hVidPn);
        }
        else
        {
            DXGKRNL_ERR("DxgkAdapterStart: mandatory DxgkVidPnCreateForAdapter failed 0x%08lX\n", Status);
            goto StartRollback;
        }
    }

    /*
     * Full WDDM adapters keep VidPN commit deferred until cdd opens the
     * shared primary. Display-only BIOS/no-GOP adapters establish a real
     * mode here so win32k never sees a synthetic fallback geometry.
     */
    if (Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        Status = DxgkDisplayEstablishInitialMode(Adapter);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkAdapterStart: mandatory DOD mode establishment failed 0x%08lX\n", Status);
            goto StartRollback;
        }
    }

    /* Initialise the per-VidPnSource present queues. */
    if (DxgkAdapterStartRoleRequiresDisplayPipeline(Role))
    {
        StepStart100ns = DxgkpTraceNow100ns();
        Status = DxgkPresentInit(Adapter);
        Adapter->PresentQueueInitializationStatus = Status;
        PresentUs = DxgkpTraceElapsedUs(StepStart100ns);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkAdapterStart: mandatory DxgkPresentInit failed 0x%08lX\n", Status);
            goto StartRollback;
        }
        Progress.PresentStarted = TRUE;
    }

    DxgkReinitializeAdapterRundown(Adapter);
    Progress.RundownReinitialized = TRUE;
    if (DxgkAdapterStartRoleRequiresScheduler(Role))
    {
        InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
        InterlockedExchange(&Adapter->VidSchStopping, 0);
    }
    if (DxgkAdapterStartRoleRequiresDisplayPipeline(Role))
        DxgkPresentResume(Adapter);

    /* Watch for stuck submissions (documented TDR recovery). */
    if (DxgkAdapterStartRoleRequiresScheduler(Role))
    {
        DxgkpStartTdrWatchdog(Adapter);
        Progress.TdrStarted = TRUE;
    }

    /*
     * Ask the miniport to deliver vsync notifications. WDDM 2.7 uses the
     * per-source ControlInterrupt3 contract when supplied; WDDM 2.0-2.6 full
     * miniports use ControlInterrupt2, while DOD can use ControlInterrupt.
     */
    if (DxgkAdapterStartRoleRequiresDisplayPipeline(Role))
    {
        Status = DxgkpSetVsyncInterruptState(Adapter, DXGK_VSYNC_ENABLE);
        DXGKRNL_TRACE("DxgkAdapterStart: CRTC_VSYNC enable -> 0x%08lX\n", Status);
        if (NT_SUCCESS(Status))
            Progress.VsyncEnabled = TRUE;
        else if (Status != STATUS_NOT_SUPPORTED)
            goto StartRollback;
    }

    /* Enable the GUID_DISPLAY_DEVICE_ARRIVAL device interface.
     * User-mode components (DXGI, OpenGL ICD loader) and kernel PnP
     * notification consumers listen for this interface to discover adapters. */
    if (Adapter->DeviceInterfaceName.Buffer == NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        DXGKRNL_ERR("DxgkAdapterStart: adapter discovery interface was not registered\n");
        goto StartRollback;
    }
    else
    {
        Status = IoSetDeviceInterfaceState(&Adapter->DeviceInterfaceName, TRUE);
        if (NT_SUCCESS(Status))
        {
            Adapter->DeviceInterfaceEnabled = TRUE;
            Progress.InterfaceEnabled = TRUE;
            DXGKRNL_TRACE("DxgkAdapterStart: enabled device interface %wZ\n",
                          &Adapter->DeviceInterfaceName);
        }
        else
        {
            DXGKRNL_ERR("DxgkAdapterStart: mandatory IoSetDeviceInterfaceState(TRUE) failed 0x%08lX\n", Status);
            goto StartRollback;
        }
    }

    /* Create \DosDevices\DISPLAY symlink pointing to \Device\DxgKrnl.
     * This is created once (first adapter to start).  If the symlink
     * already exists we silently ignore the collision. */
    if (DxgkAdapterStartRoleRequiresDisplayPipeline(Role))
    {
        static LONG DisplaySymlinkCreated = 0;
        if (InterlockedCompareExchange(&DisplaySymlinkCreated, 1, 0) == 0)
        {
            UNICODE_STRING SymlinkName, TargetName;
            NTSTATUS SymStatus;

            RtlInitUnicodeString(&SymlinkName, L"\\DosDevices\\DISPLAY");
            RtlInitUnicodeString(&TargetName,  L"\\Device\\DxgKrnl");
            SymStatus = IoCreateSymbolicLink(&SymlinkName, &TargetName);
            if (NT_SUCCESS(SymStatus))
            {
                DXGKRNL_TRACE("DxgkAdapterStart: created \\DosDevices\\DISPLAY symlink\n");
            }
            else if (SymStatus == STATUS_OBJECT_NAME_COLLISION)
            {
                DXGKRNL_TRACE("DxgkAdapterStart: \\DosDevices\\DISPLAY already exists\n");
            }
            else
            {
                DXGKRNL_WARN("DxgkAdapterStart: IoCreateSymbolicLink(DISPLAY) "
                              "failed 0x%08lX (non-fatal)\n", SymStatus);
                InterlockedExchange(&DisplaySymlinkCreated, 0);
            }
        }
    }

    /*
     * Register the display device with win32ss.
     * This creates \Device\Video0, writes the DEVICEMAP\VIDEO registry
     * entries, and writes InstalledDisplayDrivers=cdd for full WDDM
     * adapters (framebuf for display-only fallback) so that win32ss
     * EngpUpdateGraphicsDeviceList can discover the adapter and load the
     * matching display driver.
     *
     * On Windows, dxgkrnl registers with the display subsystem through a
     * different mechanism; on ReactOS we emulate the XPDM device discovery
     * path that win32ss expects.
     */
    if (DxgkAdapterStartRoleRequiresDisplayPipeline(Role))
    {
        BOOLEAN RegisterDisplayBridge;

        StepStart100ns = DxgkpTraceNow100ns();
        RegisterDisplayBridge = DxgkpShouldRegisterDisplayBridge(Adapter);
        if (RegisterDisplayBridge)
        {
            Status = DxgkDisplayRegister(Adapter);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_ERR("DxgkAdapterStart: mandatory DxgkDisplayRegister "
                            "failed 0x%08lX\n",
                            Status);
                goto StartRollback;
            }
            Progress.DisplayRegistered = TRUE;
        }
        DisplayUs = DxgkpTraceElapsedUs(StepStart100ns);
    }

    DXGKRNL_TRACE("DxgkAdapterStart: summary connect=%I64u us miniport=%I64u us vidmm=%I64u us vidpn=%I64u us present=%I64u us display=%I64u us total=%I64u us irq=%ld queue=%ld dpc=%ld\n",
                  InterruptConnectUs,
                  MiniportStartUs,
                  VidMmUs,
                  VidPnUs,
                  PresentUs,
                  DisplayUs,
                  DxgkpTraceElapsedUs(AdapterStart100ns),
                  Adapter->InterruptCount,
                  Adapter->QueueDpcCount,
                  Adapter->DpcCount);

    Status = STATUS_SUCCESS;
    DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, TRUE);
    return Status;

StartRollback:
    /* Runtime power management is brought up before the last few start steps,
     * so a rollback has to retire it; otherwise PoFx keeps a registration
     * pointing at an adapter that is being torn down. */
    DxgkpStopRuntimePowerManagement(Adapter);
    Status = DxgkpRollbackAdapterStart(Adapter, &Progress, Status, &Restartable);
    DxgkpCompleteAdapterStart(Adapter, StartGeneration, Status, Restartable);
    return Status;
}

/*
 * DxgkAdapterStop
 *
 * Called from DxgkpMiniportPnpDispatch in response to IRP_MN_STOP_DEVICE.
 * Tears down the video memory manager and calls DxgkDdiStopDevice.
 *
 * IRQL: PASSIVE_LEVEL
 */
static NTSTATUS
DxgkpStopMiniportForTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    NTSTATUS Status;

    if (Adapter->MiniportDeviceStopped)
        return STATUS_SUCCESS;
    if (Adapter->MiniportContext->InitData.s.DxgkDdiStopDevice == NULL)
        return STATUS_NOT_SUPPORTED;
    DxgkBeginKmdExclusive(Adapter);
    DxgkVidMmQuiesceAdapter(Adapter);
    Status = DxgkVidMmPrepareForIdle(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DxgkEndKmdExclusive(Adapter, FALSE);
        return Status;
    }
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        DxgkEndKmdExclusive(Adapter, FALSE);
        return STATUS_DELETE_PENDING;
    }

    _SEH2_TRY
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiStopDevice(Adapter->MiniportDeviceContext);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (NT_SUCCESS(Status))
    {
        Adapter->MiniportDeviceStopped = TRUE;
        InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    }
    else
    {
        InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    }
    DxgkReleaseMiniportCallback(Adapter);
    DxgkEndKmdExclusive(Adapter, FALSE);
    return Status;
}

static NTSTATUS
DxgkpWaitForTrackedDmaIdle(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER Delay;
    ULONGLONG Start100ns = KeQueryInterruptTime();

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
        if (KeQueryInterruptTime() - Start100ns >= (ULONGLONG)TimeoutMs * 10000)
            return STATUS_IO_TIMEOUT;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

static NTSTATUS
DxgkpResetMiniportForTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN SchedulerPrepared = FALSE;
    NTSTATUS SchedulerStatus;
    NTSTATUS Status;

    SchedulerStatus = VidSchPrepareAdapterReset(Adapter);
    if (NT_SUCCESS(SchedulerStatus))
        SchedulerPrepared = TRUE;
    else if (SchedulerStatus != STATUS_NOT_SUPPORTED)
        return SchedulerStatus;

    if (DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout) == NULL)
    {
        if (SchedulerPrepared)
            VidSchCompleteAdapterReset(Adapter, FALSE);
        return STATUS_NOT_SUPPORTED;
    }
    DxgkBeginKmdExclusive(Adapter);
    DxgkVidMmQuiesceAdapter(Adapter);
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        if (SchedulerPrepared)
            VidSchCompleteAdapterReset(Adapter, FALSE);
        DxgkEndKmdExclusive(Adapter, FALSE);
        return STATUS_DELETE_PENDING;
    }

    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiResetFromTimeout)(Adapter->MiniportDeviceContext);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseMiniportCallback(Adapter);
    if (!NT_SUCCESS(Status))
        goto ResetFailed;
    Status = DxgkVidMmRecoverFromTimeout(Adapter);
    if (!NT_SUCCESS(Status))
        goto ResetFailed;
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    if (SchedulerPrepared)
        VidSchCompleteAdapterReset(Adapter, TRUE);
    DxgkEndKmdExclusive(Adapter, FALSE);
    return Status;

ResetFailed:
    if (SchedulerPrepared)
        VidSchCompleteAdapterReset(Adapter, FALSE);
    DxgkEndKmdExclusive(Adapter, FALSE);
    return Status;
}

BOOLEAN
DxgkAcquireMiniportCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return FALSE;
    if (!DxgkAcquireKmdCall(Adapter))
        return FALSE;
    (VOID)KeWaitForSingleObject(&Adapter->MiniportCallbackMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
    {
        KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
        DxgkReleaseKmdCall(Adapter);
        return FALSE;
    }
    return TRUE;
}

BOOLEAN
DxgkAcquireMiniportCallbackFromReservedKmdCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return FALSE;
    ASSERT(InterlockedCompareExchange(&Adapter->KmdActiveCalls, 0, 0) > 0);
    (VOID)KeWaitForSingleObject(&Adapter->MiniportCallbackMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
    {
        KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
        DxgkReleaseKmdCall(Adapter);
        return FALSE;
    }
    return TRUE;
}

VOID
DxgkReleaseMiniportCallback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ASSERT(Adapter != NULL);
    KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
    DxgkReleaseKmdCall(Adapter);
}

BOOLEAN
DxgkAcquireKmdCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVOID CurrentThread;

    if (Adapter == NULL)
        return FALSE;
    CurrentThread = PsGetCurrentThread();
    if (InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) != 0 && Adapter->KmdExclusiveOwnerThread != CurrentThread && Adapter->KmdTransactionOwnerThread != CurrentThread)
        return FALSE;
    InterlockedIncrement(&Adapter->KmdActiveCalls);
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) != 0 && Adapter->KmdExclusiveOwnerThread != CurrentThread && Adapter->KmdTransactionOwnerThread != CurrentThread)
    {
        DxgkReleaseKmdCall(Adapter);
        return FALSE;
    }
    return TRUE;
}

VOID
DxgkReleaseKmdCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG Remaining;

    ASSERT(Adapter != NULL);
    Remaining = InterlockedDecrement(&Adapter->KmdActiveCalls);
    ASSERT(Remaining >= 0);
}

VOID
DxgkAcquireLevel3Transition(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVOID CurrentThread;

    PAGED_CODE();
    ASSERT(Adapter != NULL);
    CurrentThread = PsGetCurrentThread();
    if (Adapter->Level3TransitionOwnerThread == CurrentThread)
    {
        ASSERT(InterlockedCompareExchange(&Adapter->Level3TransitionDepth, 0, 0) > 0);
        InterlockedIncrement(&Adapter->Level3TransitionDepth);
        return;
    }
    (VOID)KeWaitForSingleObject(&Adapter->Level3TransitionMutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Adapter->Level3TransitionOwnerThread == NULL);
    ASSERT(InterlockedCompareExchange(&Adapter->Level3TransitionDepth, 0, 0) == 0);
    InterlockedExchange(&Adapter->Level3TransitionDepth, 1);
    KeMemoryBarrier();
    Adapter->Level3TransitionOwnerThread = CurrentThread;
    KeMemoryBarrier();
}

VOID
DxgkReleaseLevel3Transition(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG Depth;

    PAGED_CODE();
    ASSERT(Adapter != NULL);
    ASSERT(Adapter->Level3TransitionOwnerThread == PsGetCurrentThread());
    Depth = InterlockedDecrement(&Adapter->Level3TransitionDepth);
    ASSERT(Depth >= 0);
    if (Depth != 0)
        return;
    Adapter->Level3TransitionOwnerThread = NULL;
    KeMemoryBarrier();
    KeReleaseMutex(&Adapter->Level3TransitionMutex, FALSE);
}

VOID
DxgkBeginKmdExclusive(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;

    PAGED_CODE();
    ASSERT(Adapter != NULL);
    (VOID)KeWaitForSingleObject(&Adapter->KmdExclusiveMutex, Executive, KernelMode, FALSE, NULL);
    InterlockedExchange(&Adapter->KmdCallsBlocked, 1);
    KeMemoryBarrier();
    Delay.QuadPart = -(LONGLONG)(10 * 1000);
    while (InterlockedCompareExchange(&Adapter->KmdActiveCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    Adapter->KmdExclusiveOwnerThread = PsGetCurrentThread();
    KeMemoryBarrier();
}

VOID
DxgkEndKmdExclusive(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN ReopenAdmission)
{
    PAGED_CODE();
    ASSERT(Adapter != NULL);
    ASSERT(Adapter->KmdExclusiveOwnerThread == PsGetCurrentThread());
    Adapter->KmdExclusiveOwnerThread = NULL;
    KeMemoryBarrier();
    if (ReopenAdmission)
        InterlockedExchange(&Adapter->KmdCallsBlocked, 0);
    KeReleaseMutex(&Adapter->KmdExclusiveMutex, FALSE);
    if (ReopenAdmission)
        DxgkVidMmKickDeferredDestroyBatches(Adapter);
}

static NTSTATUS
DxgkpAdapterStopInternal(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN ReleasePostDisplayOwnership,
    _In_ DXGMMS2_STOP_REASON StopReason,
    _Out_opt_ PDXGK_DISPLAY_INFORMATION ReleasedPostDisplayInformation,
    _Out_opt_ PBOOLEAN ReleasedByDriver)
{
    NTSTATUS Status;
    NTSTATUS SchedulerIdleStatus;
    NTSTATUS TrackerIdleStatus;
    NTSTATUS VsyncStatus;
    PDEVICE_OBJECT FunctionalDeviceObject;
    BOOLEAN SchedulerAlreadyPrepared;
    BOOLEAN StopDeviceEstablishedBoundary = FALSE;
    BOOLEAN StopChangedState = FALSE;
    ULONG StopGeneration = 0;

    PAGED_CODE();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);
    if (ReleasedPostDisplayInformation != NULL)
    {
        RtlZeroMemory(ReleasedPostDisplayInformation,
                      sizeof(*ReleasedPostDisplayInformation));
    }
    if (ReleasedByDriver != NULL)
        *ReleasedByDriver = FALSE;

    DXGKRNL_TRACE("DxgkAdapterStop: Adapter %p\n", Adapter);

    FunctionalDeviceObject = Adapter->FunctionalDeviceObject;
    if (FunctionalDeviceObject != NULL)
        ObReferenceObject(FunctionalDeviceObject);

    DxgkpAcquireAdapterMutexAfterStart(Adapter);
    if (InterlockedCompareExchange(&Adapter->AdapterStopInProgress, 0, 0) != 0)
    {
        StopGeneration = Adapter->AdapterStopGeneration;
        ASSERT(StopGeneration != 0);
        Adapter->AdapterStopIntentCount++;
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        for (;;)
        {
            KeWaitForSingleObject(&Adapter->AdapterStopCompletedEvent, Executive, KernelMode, FALSE, NULL);
            (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
            if (Adapter->AdapterStopCompletedGeneration == StopGeneration)
            {
                Status = Adapter->AdapterStopStatus;
                ASSERT(Adapter->AdapterStopIntentCount > 0);
                Adapter->AdapterStopIntentCount--;
                KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
                break;
            }
            KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        }
        if (FunctionalDeviceObject != NULL)
            ObDereferenceObject(FunctionalDeviceObject);
        return Status;
    }
    if (Adapter->AdapterStopIntentCount != 0)
    {
        ASSERT(Adapter->AdapterStopCompletedGeneration == Adapter->AdapterStopGeneration);
        Status = Adapter->AdapterStopStatus;
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        if (FunctionalDeviceObject != NULL)
            ObDereferenceObject(FunctionalDeviceObject);
        return Status;
    }
    if (Adapter->State == DxgkAdapterStateStarted || Adapter->State == DxgkAdapterStateStopping)
    {
        StopGeneration = Adapter->AdapterStopGeneration + 1;
        if (StopGeneration == 0)
            StopGeneration = 1;
        Adapter->AdapterStopGeneration = StopGeneration;
        Adapter->AdapterStopStatus = STATUS_PENDING;
        Adapter->AdapterStopIntentCount++;
        KeResetEvent(&Adapter->AdapterStopCompletedEvent);
        InterlockedExchange(&Adapter->AdapterStopInProgress, 1);
        if (Adapter->State == DxgkAdapterStateStarted)
        {
            Adapter->State = DxgkAdapterStateStopping;
            StopChangedState = TRUE;
        }
    }
    else
    {
        DXGKRNL_ADAPTER_STATE State = Adapter->State;

        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        DXGKRNL_WARN("DxgkAdapterStop: adapter not started (State=%d)\n", State);
        if (FunctionalDeviceObject != NULL)
            ObDereferenceObject(FunctionalDeviceObject);
        return STATUS_SUCCESS;
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

    Status = DxgkpMms2BeginStop(Adapter, StopReason);
    if (!NT_SUCCESS(Status))
    {
        (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
        if (StopChangedState && Adapter->State == DxgkAdapterStateStopping)
            Adapter->State = DxgkAdapterStateStarted;
        Adapter->AdapterStopStatus = Status;
        Adapter->AdapterStopCompletedGeneration = StopGeneration;
        InterlockedExchange(&Adapter->AdapterStopInProgress, 0);
        ASSERT(Adapter->AdapterStopIntentCount > 0);
        Adapter->AdapterStopIntentCount--;
        KeSetEvent(&Adapter->AdapterStopCompletedEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
        if (FunctionalDeviceObject != NULL)
            ObDereferenceObject(FunctionalDeviceObject);
        return Status;
    }

    /* Retire runtime power management while the miniport is still fully
     * callable, so it receives GUID_DXGKDDI_POWER_MANAGEMENT_STOPPED and
     * stops referencing the PoFx handle it was given at start. */
    DxgkpStopRuntimePowerManagement(Adapter);

    VsyncStatus =
        DxgkpSetVsyncInterruptState(
            Adapter,
            DXGK_VSYNC_DISABLE_NO_PHASE);
    if (!NT_SUCCESS(VsyncStatus) &&
        VsyncStatus != STATUS_NOT_SUPPORTED)
    {
        DXGKRNL_WARN("DxgkAdapterStop: CRTC_VSYNC disable failed "
                     "0x%08lX\n",
                     VsyncStatus);
    }

    InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
    DxgkPresentBeginStop(Adapter);
    DxgkMarkAdapterDevicesStopped(Adapter);
    DxgkBeginAdapterRundown(Adapter);
    SchedulerAlreadyPrepared = (InterlockedCompareExchange(&Adapter->VidSchStopping, 0, 0) != 0);

    /* Stop the TDR watchdog before the miniport goes away. */
    DxgkpStopTdrWatchdog(Adapter);
    DxgkpWaitForFlagClear(&Adapter->HotPlugWorkActive);
    ASSERT(DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(&Adapter->HotPlugWorkActive));
    DxgkAcquireLevel3Transition(Adapter);

    /* Present work and already-admitted reservations drain while completion
     * DPCs remain active, so accepted GPU work can finish normally. */
    DxgkPresentTeardown(Adapter);
    DxgkWaitForSubmitDmaReservations(Adapter);

    if (!SchedulerAlreadyPrepared)
    {
        SchedulerIdleStatus = VidSchBeginStopDrain(Adapter);
        if (NT_SUCCESS(SchedulerIdleStatus))
            SchedulerIdleStatus = VidSchWaitForIdle(Adapter, 1000);
        TrackerIdleStatus = DxgkpWaitForTrackedDmaIdle(Adapter, 1000);
        if ((!NT_SUCCESS(SchedulerIdleStatus) && SchedulerIdleStatus != STATUS_NOT_SUPPORTED) || !NT_SUCCESS(TrackerIdleStatus))
        {
            Status = DxgkpResetMiniportForTeardown(Adapter);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_ERR("DxgkAdapterStop: GPU could not be drained or reset (scheduler=0x%08lX tracker=0x%08lX reset=0x%08lX); retaining all owned storage\n", SchedulerIdleStatus, TrackerIdleStatus, Status);
                goto CompleteStop;
            }
        }
    }

    /* Completion or ResetFromTimeout is the DMA ownership boundary. Close
     * every late ISR/DPC path before releasing tracker or scheduler state. */
    VidSchPrepareForStop(Adapter);
    DxgkpDisablePeriodicInterruptHandoff(Adapter);
    KeRemoveQueueDpc(&Adapter->DpcObject);
    KeFlushQueuedDpcs();
    if (InterlockedCompareExchange(&Adapter->TdrOwnershipUncertain, 0, 0) != 0)
    {
        Status = DxgkpStopMiniportForTeardown(Adapter);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkAdapterStop: StopDevice could not establish the DMA ownership boundary 0x%08lX; retaining all owned storage\n", Status);
            goto CompleteStop;
        }
        Adapter->MiniportDeviceStopped = TRUE;
        StopDeviceEstablishedBoundary = TRUE;
        InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    }
    if (StopDeviceEstablishedBoundary)
    {
        VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
        DxgkVidMmQuiesceAdapter(Adapter);
    }
    DxgkReleaseTrackedDmaBuffers(Adapter, !StopDeviceEstablishedBoundary);
    DxgkWaitForDeviceLifecycleOperations(Adapter);

    /* No scheduler/VidMm storage can disappear while a generic adapter,
     * device, context, present, or tracker producer still owns it. */
    Status = DxgkCleanupAdapterDevices(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: device cleanup failed 0x%08lX; retaining devices for RemoveDevice\n", Status);
        goto CompleteStop;
    }
    DxgkWaitForAdapterRundown(Adapter);
    if (!StopDeviceEstablishedBoundary)
        DxgkVidMmQuiesceAdapter(Adapter);

    Status = DxgkDestroyPagingSystemContext(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: paging system context cleanup failed 0x%08lX\n",
                    Status);
        goto CompleteStop;
    }

    if (ReleasePostDisplayOwnership && !Adapter->MiniportDeviceStopped)
    {
        PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP PfnRelease = DXGK_CB(Adapter, DxgkDdiStopDeviceAndReleasePostDisplayOwnership);

        if (PfnRelease != NULL)
        {
            DXGK_DISPLAY_INFORMATION LocalReleasedInfo;
            PDXGK_DISPLAY_INFORMATION ReleasedInfo;

            ReleasedInfo = ReleasedPostDisplayInformation != NULL ?
                               ReleasedPostDisplayInformation :
                               &LocalReleasedInfo;
            RtlZeroMemory(ReleasedInfo, sizeof(*ReleasedInfo));
            DxgkBeginKmdExclusive(Adapter);
            if (DxgkAcquireMiniportCallback(Adapter))
            {
                _SEH2_TRY
                {
                    Status = PfnRelease(Adapter->MiniportDeviceContext,
                                        0,
                                        ReleasedInfo);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (NT_SUCCESS(Status))
                {
                    Adapter->MiniportDeviceStopped = TRUE;
                    if (ReleasedByDriver != NULL)
                        *ReleasedByDriver = TRUE;
                }
                else
                {
                    RtlZeroMemory(ReleasedInfo, sizeof(*ReleasedInfo));
                    DXGKRNL_WARN("DxgkpStopPostDisplayOwner: StopDeviceAndReleasePostDisplayOwnership failed 0x%08lX\n", Status);
                }
                DxgkReleaseMiniportCallback(Adapter);
            }
            DxgkEndKmdExclusive(Adapter, FALSE);
        }
    }

    Status = DxgkpStopMiniportForTeardown(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: DxgkDdiStopDevice remained failed 0x%08lX; retaining scheduler/VidMm state for RemoveDevice\n", Status);
        goto CompleteStop;
    }
    Status = DxgkpCloseAndRetireReverseCallbacks(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: reverse-callback retirement failed 0x%08lX; retaining software state for RemoveDevice\n", Status);
        goto CompleteStop;
    }

    VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
    DxgkpDisconnectAdapterInterrupt(Adapter);
    Status = DxgkCleanupAdapterDevices(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: context-stream cleanup failed 0x%08lX; retaining scheduler/VidMm state for retry\n", Status);
        goto CompleteStop;
    }
    Status = DxgkpMms2CompleteRetiredStop(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkAdapterStop: dxgmms2 completion failed 0x%08lX; retaining scheduler/VidMm state for retry\n", Status);
        goto CompleteStop;
    }
    VidSchDestroy(Adapter);

    /* Unregister the display device from win32ss. */
    DxgkDisplayUnregister(Adapter);

    /* Tear down the VidPN. */
    {
        D3DKMDT_HVIDPN hVidPn;

        (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
        hVidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
        Adapter->VidPn = NULL;
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        if (hVidPn != NULL)
            DxgkVidPnDestroy(hVidPn);
    }

    /* Tracker retirement can reference both objects, so destroy them last. */
    DxgkDestroySharedPrimary(Adapter);
    /* Pooled segment-backed DMA buffers retain VidMm allocations and pins. */
    DxgkpDrainDmaBufferCache(Adapter);
    DxgkVidMmTeardownAdapter(Adapter);

    DxgkpClearPostDisplayOwner(Adapter);

    DxgkpReleaseAdapterResources(Adapter);
    Adapter->InterruptTraceEpoch100ns = 0;
    DxgkpReleasePostDisplayMapping(Adapter);

    DXGKRNL_TRACE("DxgkAdapterStop: stopped\n");

CompleteStop:
    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Adapter->AdapterStopGeneration == StopGeneration);
    ASSERT(Adapter->AdapterStopIntentCount > 0);
    if (NT_SUCCESS(Status) && Adapter->State == DxgkAdapterStateStopping)
        Adapter->State = DxgkAdapterStateStopped;
    Adapter->AdapterStopStatus = Status;
    Adapter->AdapterStopCompletedGeneration = StopGeneration;
    InterlockedExchange(&Adapter->AdapterStopInProgress, 0);
    Adapter->AdapterStopIntentCount--;
    KeSetEvent(&Adapter->AdapterStopCompletedEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    DxgkReleaseLevel3Transition(Adapter);
    if (FunctionalDeviceObject != NULL)
        ObDereferenceObject(FunctionalDeviceObject);
    return Status;
}

NTSTATUS
DxgkAdapterStop(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    return DxgkpAdapterStopInternal(Adapter,
                                    FALSE,
                                    Dxgmms2StopReasonPnpStop,
                                    NULL,
                                    NULL);
}

/*
 * DxgkAdapterRemove
 *
 * Called from DxgkpMiniportPnpDispatch in response to IRP_MN_REMOVE_DEVICE
 * (or IRP_MN_SURPRISE_REMOVAL + IRP_MN_REMOVE_DEVICE).  Stops the adapter
 * if still started, calls DxgkDdiRemoveDevice, disconnects the interrupt,
 * frees descriptor arrays, and unlinks from the per-miniport and global lists.
 * The PnP dispatch routine forwards the remove IRP before it performs the final
 * detach and FDO deletion.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkAdapterRemove(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;
    NTSTATUS StopStatus = STATUS_SUCCESS;
    PVOID MiniportDeviceContext;
    NTSTATUS CleanupStatus;
    BOOLEAN MiniportCleanupBeforeRemove = FALSE;
    BOOLEAN MiniportCleanupCallbacksPermitted = FALSE;

    PAGED_CODE();

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);

    DXGKRNL_TRACE("DxgkAdapterRemove: Adapter %p State=%d\n",
                  Adapter, Adapter->State);

    /* A remove cannot close admission or callbacks underneath StartDevice. */
    DxgkpAcquireAdapterMutexAfterStart(Adapter);
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    if (InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 1, 0) == 0)
        ExWaitForRundownProtectionRelease(&Adapter->RemoveRundownRef);
    DxgkpDrainDmaBufferCache(Adapter);

    /* Stop the adapter if it is still running. */
    InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
    DxgkPresentBeginStop(Adapter);
    DxgkMarkAdapterDevicesStopped(Adapter);
    DxgkBeginAdapterRundown(Adapter);
    if (InterlockedCompareExchange(&Adapter->AdapterStopInProgress, 0, 0) != 0 || Adapter->State == DxgkAdapterStateStarting || Adapter->State == DxgkAdapterStateStarted || Adapter->State == DxgkAdapterStateStopping)
    {
        StopStatus = DxgkpAdapterStopInternal(Adapter,
                                              FALSE,
                                              Dxgmms2StopReasonRemove,
                                              NULL,
                                              NULL);
        MiniportCleanupBeforeRemove = NT_SUCCESS(StopStatus);
    }
    else if (Adapter->State == DxgkAdapterStateStopped)
    {
        MiniportCleanupBeforeRemove = TRUE;
    }
    else if (Adapter->State == DxgkAdapterStateSurpriseRemoved)
    {
        DxgkpStopTdrWatchdog(Adapter);
        DxgkPresentTeardown(Adapter);
        VidSchPrepareForStop(Adapter);
        DxgkpDisablePeriodicInterruptHandoff(Adapter);
        KeRemoveQueueDpc(&Adapter->DpcObject);
        KeFlushQueuedDpcs();
        DxgkWaitForSubmitDmaReservations(Adapter);
        MiniportCleanupBeforeRemove = Adapter->SurpriseRemovalHandled || Adapter->MiniportDeviceStopped;
        MiniportCleanupCallbacksPermitted = Adapter->SurpriseRemovalHandled;
    }
    if (!NT_SUCCESS(StopStatus))
        DXGKRNL_ERR("DxgkAdapterRemove: StopDevice remained failed 0x%08lX; deferring DMA release until RemoveDevice\n", StopStatus);

    /* Close every scheduler/interrupt producer before device or VidMm
     * cleanup. Failed StopDevice paths have not necessarily done this yet. */
    VidSchPrepareForStop(Adapter);
    DxgkpDisconnectAdapterInterrupt(Adapter);
    KeRemoveQueueDpc(&Adapter->DpcObject);
    KeFlushQueuedDpcs();
    DxgkpWaitForVidSchCallbacks(Adapter);
    /* Keep public KMD admission closed through software cleanup and the final
     * RemoveDevice callback.  The exclusive owner may still issue the cleanup
     * DDIs that a successful surprise-removal notification permits. */
    DxgkBeginKmdExclusive(Adapter);
    if (MiniportCleanupBeforeRemove)
    {
        VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
        DxgkReleaseTrackedDmaBuffers(Adapter, MiniportCleanupCallbacksPermitted);
        DxgkWaitForDeviceLifecycleOperations(Adapter);
        CleanupStatus = DxgkCleanupAdapterDevices(Adapter);
        if (NT_SUCCESS(CleanupStatus))
        {
            DxgkWaitForAdapterRundown(Adapter);
            DxgkVidMmQuiesceAdapter(Adapter);
        }
        else
        {
            DXGKRNL_ERR("DxgkAdapterRemove: pre-RemoveDevice cleanup failed 0x%08lX; deferring retained devices to the final boundary\n", CleanupStatus);
            MiniportCleanupBeforeRemove = FALSE;
        }
    }

    /* Disable and free the GUID_DISPLAY_DEVICE_ARRIVAL device interface. */
    if (Adapter->DeviceInterfaceEnabled)
    {
        IoSetDeviceInterfaceState(&Adapter->DeviceInterfaceName, FALSE);
        Adapter->DeviceInterfaceEnabled = FALSE;
    }
    if (Adapter->DeviceInterfaceName.Buffer != NULL)
    {
        RtlFreeUnicodeString(&Adapter->DeviceInterfaceName);
        RtlInitUnicodeString(&Adapter->DeviceInterfaceName, NULL);
    }

    /* Stop display dispatch and its present worker while callbacks remain
     * valid, then prevent bugcheck-time display callbacks from retaining the
     * adapter past the final miniport removal boundary. */
    DxgkDisplayUnregister(Adapter);
    DxgkpClearPostDisplayOwner(Adapter);

    /* Close the callback gate and detach the opaque context before invoking
     * the final DDI. All subsequent cleanup is OS bookkeeping only. */
    (VOID)KeWaitForSingleObject(&Adapter->MiniportCallbackMutex, Executive, KernelMode, FALSE, NULL);
    MiniportDeviceContext = Adapter->MiniportDeviceContext;
    InterlockedExchange(&Adapter->MiniportCallbacksValid, 0);
    Adapter->MiniportDeviceContext = NULL;
    if (MiniportDeviceContext != NULL && Adapter->MiniportContext->InitData.s.DxgkDdiRemoveDevice != NULL)
    {
        NTSTATUS Status = DxgkpRemoveMiniportDevice(Adapter, MiniportDeviceContext);
        if (!NT_SUCCESS(Status))
            DXGKRNL_ERR("DxgkAdapterRemove: DxgkDdiRemoveDevice failed 0x%08lX (continuing)\n", Status);
    }
    if (InterlockedCompareExchange(&Adapter->ReverseCallbackRundownStarted, 1, 0) == 0)
        ExWaitForRundownProtectionRelease(&Adapter->ReverseCallbackRundownRef);
    KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
    DxgkpReleaseMapMemory(Adapter);
    DxgkpReleasePciBusInterface(Adapter);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    DxgkpReleaseCallbackMemory(Adapter);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    DxgkpReleasePhysicalMemoryObjects(Adapter);
#endif
    DxgkpFreeAdapterRegistryPath(Adapter);
    DxgkEndKmdExclusive(Adapter, FALSE);

    /* RemoveDevice is the final hardware-ownership boundary when StopDevice
     * could not complete. Only now may OS tracking force-release storage. */
    Adapter->MiniportDeviceStopped = TRUE;
    Adapter->MiniportRemoveDeviceComplete = TRUE;
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 0);
    VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
    if (!MiniportCleanupBeforeRemove)
    {
        DxgkReleaseTrackedDmaBuffers(Adapter, FALSE);
        DxgkWaitForDeviceLifecycleOperations(Adapter);
        DxgkVidMmQuiesceAdapter(Adapter);
        CleanupStatus = DxgkCleanupAdapterDevices(Adapter);
        if (!NT_SUCCESS(CleanupStatus))
            DxgkpBugCheckMms2Lifecycle(Adapter, CleanupStatus, DXGKP_MMS2_FAILURE_FINAL_RETIREMENT);
        DxgkWaitForAdapterRundown(Adapter);
    }

    {
        DXGMMS2_STOP_REASON FinalReason;
        NTSTATUS Mms2Status;

        FinalReason = Adapter->State == DxgkAdapterStateSurpriseRemoved ? Dxgmms2StopReasonSurpriseRemove : Dxgmms2StopReasonRemove;
        Mms2Status = DxgkpMms2BeginStop(Adapter, FinalReason);
        if (NT_SUCCESS(Mms2Status))
            Mms2Status = DxgkpMms2CompleteRetiredStop(Adapter);
        if (!NT_SUCCESS(Mms2Status))
            DxgkpBugCheckMms2Lifecycle(Adapter, Mms2Status, DXGKP_MMS2_FAILURE_FINAL_RETIREMENT);
    }

    VidSchDestroy(Adapter);
    {
        D3DKMDT_HVIDPN hVidPn;

        (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
        hVidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
        Adapter->VidPn = NULL;
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        if (hVidPn != NULL)
            DxgkVidPnDestroy(hVidPn);
    }
    DxgkDestroySharedPrimary(Adapter);
    DxgkpDrainDmaBufferCache(Adapter);
    DxgkVidMmTeardownAdapter(Adapter);
    (VOID)DxgkCleanupAdapterDevices(Adapter);
    DxgkpReleaseAdapterResources(Adapter);
    DxgkpReleasePostDisplayMapping(Adapter);

    /* Delete all child PDOs. */
    while (!IsListEmpty(&Adapter->ChildListHead))
    {
        PLIST_ENTRY Entry;
        PDXGK_CHILD_PDO_EXTENSION ChildExt;

        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (IsListEmpty(&Adapter->ChildListHead))
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            break;
        }
        Entry = RemoveHeadList(&Adapter->ChildListHead);
        if (Adapter->ChildPdoCount > 0)
            Adapter->ChildPdoCount--;
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);

        ChildExt = CONTAINING_RECORD(Entry,
                                     DXGK_CHILD_PDO_EXTENSION,
                                     ListEntry);
        InitializeListHead(&ChildExt->ListEntry);
        ChildExt->ParentAdapter = NULL;
        DxgkpDeleteChildPdo(ChildExt);
    }

    /* Free descriptor arrays. */
    if (Adapter->ChildDescriptors != NULL)
    {
        ExFreePoolWithTag(Adapter->ChildDescriptors, TAG_DXGK_RESOURCES);
        Adapter->ChildDescriptors = NULL;
    }
    /*
     * Adapter->Segments (DXGKRNL_SEGMENT array) is freed by
     * DxgkVidMmTeardownAdapter which is called earlier in DxgkAdapterStop.
     * Do not double-free here.
     */

    {
        NTSTATUS Mms2Status;

        Mms2Status = DxgkpMms2DestroyAdministrativeAdapter(Adapter);
        if (!NT_SUCCESS(Mms2Status))
            DxgkpBugCheckMms2Lifecycle(Adapter, Mms2Status, DXGKP_MMS2_FAILURE_FINAL_DESTROY);
    }

    /* Unlink from per-miniport adapter list. */
    KeAcquireSpinLock(&Adapter->MiniportContext->AdapterListLock, &OldIrql);
    RemoveEntryList(&Adapter->MiniportAdapterListEntry);
    InitializeListHead(&Adapter->MiniportAdapterListEntry);
    KeReleaseSpinLock(&Adapter->MiniportContext->AdapterListLock, OldIrql);

    /* Unlink from global adapter list. */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    RemoveEntryList(&Adapter->GlobalAdapterListEntry);
    InitializeListHead(&Adapter->GlobalAdapterListEntry);
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);

    Adapter->State = DxgkAdapterStateRemoved;
    DXGKRNL_TRACE("DxgkAdapterRemove: teardown complete\n");
}

static VOID
DxgkpDeleteRemovedAdapterFdo(_In_ PDXGKRNL_ADAPTER Adapter)
{
    PDEVICE_OBJECT FunctionalDeviceObject;
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    KIRQL OldIrql;

    DxgkRosAssert(Adapter != NULL, DXGKRNL_BUGCHECK_NULL_ADAPTER);
    MpCtx = Adapter->MiniportContext;
    FunctionalDeviceObject = Adapter->FunctionalDeviceObject;
    if (Adapter->LowerDeviceObject != NULL)
    {
        IoDetachDevice(Adapter->LowerDeviceObject);
        Adapter->LowerDeviceObject = NULL;
    }
    DXGKRNL_TRACE("DxgkpDeleteRemovedAdapterFdo: deleting FDO %p\n", FunctionalDeviceObject);
    IoDeleteDevice(FunctionalDeviceObject);
    KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
    DxgkRosAssert(MpCtx->AdapterCount > 0, DXGKRNL_BUGCHECK_BAD_DEVICE_EXT);
    MpCtx->AdapterCount--;
    KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);
}

/* A native Windows dump rooted at dxgkrnl!DpiFdoHandleSurpriseRemoval uses
 * VIDEO_DXGKRNL_FATAL_ERROR (0x113), subtype 0x19.  ReactOS has no dxgkrnl
 * graceful-reboot handoff, so this is the closest non-silent containment for
 * the documented "no more miniport DDIs and reboot" failure contract. */
static DECLSPEC_NORETURN VOID
DxgkpBugCheckSurpriseRemoval(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ NTSTATUS FailureStatus,
    _In_opt_ PDXGKDDI_NOTIFY_SURPRISE_REMOVAL NotifyCallback)
{
    InterlockedExchange(&Adapter->KmdCallsBlocked, 1);
    InterlockedExchange(&Adapter->InterruptCallbacksBlocked, 1);
    InterlockedExchange(&Adapter->MiniportCallbacksValid, 0);
    KeMemoryBarrier();
    DXGKRNL_ERR("DxgkpBugCheckSurpriseRemoval: adapter %p cannot contain surprise removal, status 0x%08lX callback %p\n", Adapter, FailureStatus, (PVOID)NotifyCallback);
    KeBugCheckEx(DXGKP_BUGCHECK_VIDEO_DXGKRNL_FATAL_ERROR, (ULONG_PTR)DXGKP_FATAL_SURPRISE_REMOVAL_SUBTYPE, (ULONG_PTR)FailureStatus, (ULONG_PTR)Adapter, (ULONG_PTR)NotifyCallback);
}

/* ========================================================================
 * PnP and Power dispatch routines (installed into miniport DriverObject)
 * ====================================================================== */

/*
 * DxgkpStartDeviceCompletion
 *
 * IoCompletion routine for IRP_MN_START_DEVICE.  Signals the event passed
 * as Context and returns STATUS_MORE_PROCESSING_REQUIRED so that the IRP
 * is not completed twice.  The dispatch routine waits on the event and
 * then completes the IRP itself.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
static NTSTATUS
NTAPI
DxgkpStartDeviceCompletion(
    _In_     PDEVICE_OBJECT DeviceObject,
    _In_     PIRP           Irp,
    _In_opt_ PVOID          Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    if (Context != NULL)
        KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * DxgkpMiniportPnpDispatch
 *
 * IRP_MJ_PNP handler installed into the miniport DriverObject.
 * Handles the subset of PnP minor codes that affect adapter state;
 * all others are forwarded to the lower device object.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkpMiniportPnpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PDXGKRNL_ADAPTER   Adapter;
    PIO_STACK_LOCATION Stack;
    NTSTATUS           Status;
    ULONGLONG          IrpStart100ns;
    ULONGLONG          LowerStart100ns;
    ULONGLONG          LowerUs = 0;
    ULONGLONG          AdapterStartUs = 0;

    PAGED_CODE();

    /*
     * Route IRPs for non-adapter devices first.
     *
     * The miniport DriverObject is shared by the adapter FDO, child PDOs,
     * \Device\Video0 (display device), and \Device\DxgKrnl (control device).
     * Check these before treating the IRP as an adapter FDO IRP.
     */

    /* Route \Device\Video0 PnP IRPs to the display handler. */
    if (DxgkDisplayDispatchPnp(DeviceObject, Irp))
        return STATUS_SUCCESS;

    /* Route \Device\DxgKrnl PnP IRPs — complete with success (no-op). */
    if (GDxgControlDeviceObject != NULL && DeviceObject == GDxgControlDeviceObject)
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    /*
     * Determine whether this IRP targets a child PDO or the GPU FDO.
     *
     * Both the FDO and child PDOs share the same DriverObject dispatch
     * table (because IoCreateDevice uses the FDO's DriverObject).  The
     * first ULONG of the DeviceExtension distinguishes them:
     *
     *   Child PDO  → DXGK_CHILD_PDO_SIGNATURE (0x43786744)
     *   GPU FDO    → PDXGKRNL_MINIPORT_CONTEXT pointer (never == signature)
     *
     * If the device is a child PDO, route to the child-specific handler.
     */
    {
        PULONG Signature = (PULONG)DeviceObject->DeviceExtension;
        if (Signature != NULL && *Signature == DXGK_CHILD_PDO_SIGNATURE)
        {
            return DxgkpChildPdoPnpDispatch(DeviceObject, Irp);
        }
    }

    Adapter = DXGKRNL_ADAPTER_FROM_DEVOBJ(DeviceObject);
    Stack   = IoGetCurrentIrpStackLocation(Irp);

    DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: Adapter %p Minor=%u\n",
                  Adapter, Stack->MinorFunction);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            IrpStart100ns = DxgkpTraceNow100ns();
            /*
             * Forward IRP_MN_START_DEVICE to the lower stack first so that
             * the bus driver can assign resources, then call DxgkAdapterStart.
             *
             * Pattern: copy stack, install a completion routine that sets an
             * event, call the lower driver, wait if pending, then complete.
             */
            {
                KEVENT Event;
                KeInitializeEvent(&Event, NotificationEvent, FALSE);

                IoCopyCurrentIrpStackLocationToNext(Irp);
                IoSetCompletionRoutine(Irp,
                                       DxgkpStartDeviceCompletion,
                                       &Event,
                                       TRUE, TRUE, TRUE);

                LowerStart100ns = DxgkpTraceNow100ns();
                Status = IoCallDriver(Adapter->LowerDeviceObject, Irp);
                if (Status == STATUS_PENDING)
                {
                    KeWaitForSingleObject(&Event, Executive, KernelMode,
                                         FALSE, NULL);
                    Status = Irp->IoStatus.Status;
                }
                LowerUs = DxgkpTraceElapsedUs(LowerStart100ns);
            }

            DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: IRP_MN_START_DEVICE lower stack status=0x%08lX time=%I64u us\n",
                          Status,
                          LowerUs);

            if (NT_SUCCESS(Status))
            {
                LowerStart100ns = DxgkpTraceNow100ns();
                Status = DxgkAdapterStart(
                             Adapter,
                             Stack->Parameters.StartDevice.AllocatedResources,
                             Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
                AdapterStartUs = DxgkpTraceElapsedUs(LowerStart100ns);
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: IRP_MN_START_DEVICE complete status=0x%08lX lower=%I64u us port=%I64u us total=%I64u us\n",
                          Status,
                          LowerUs,
                          AdapterStartUs,
                          DxgkpTraceElapsedUs(IrpStart100ns));
            return Status;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            DEVICE_RELATION_TYPE RelType =
                Stack->Parameters.QueryDeviceRelations.Type;

            DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: "
                          "QUERY_DEVICE_RELATIONS Type=%d\n", RelType);

            if (RelType == BusRelations)
            {
                /*
                 * BusRelations: enumerate child devices (monitors) by
                 * calling DxgkDdiQueryChildRelations on the miniport and
                 * creating PDOs for each reported child.
                 *
                 * Only enumerate if the adapter has been started and the
                 * miniport reported at least one child.
                 */
                if (Adapter->State == DxgkAdapterStateStarted &&
                    Adapter->MiniportContext->InitData.s.DxgkDdiQueryChildRelations != NULL)
                {
                    PDEVICE_RELATIONS Relations = NULL;

                    Status = DxgkpQueryBusRelations(Adapter, &Relations);
                    if (NT_SUCCESS(Status) && Relations != NULL)
                    {
                        DXGKRNL_TRACE("DxgkpMiniportPnpDispatch: "
                                      "BusRelations returning %lu children\n",
                                      Relations->Count);

                        Irp->IoStatus.Information = (ULONG_PTR)Relations;
                        Irp->IoStatus.Status      = STATUS_SUCCESS;
                    }
                    else if (!NT_SUCCESS(Status))
                    {
                        DXGKRNL_ERR("DxgkpMiniportPnpDispatch: "
                                    "DxgkpQueryBusRelations failed 0x%08lX\n",
                                    Status);
                        /*
                         * On failure, forward the IRP so the lower driver
                         * can still report its own bus relations (if any).
                         */
                    }
                }

                /*
                 * Forward BusRelations to the lower driver.  The PnP
                 * manager merges our children with any the bus driver
                 * may report.
                 */
                return DxgkpForwardIrp(Adapter, Irp);
            }
            else if (RelType == TargetDeviceRelation)
            {
                /*
                 * TargetDeviceRelation: return this FDO's underlying PDO.
                 * The PnP manager uses this to find the physical device
                 * for handle-based APIs.
                 */
                PDEVICE_RELATIONS Rel;

                Rel = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
                          PagedPool,
                          sizeof(DEVICE_RELATIONS),
                          TAG_DXGK_RESOURCES);
                if (Rel == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }

                Rel->Count      = 1;
                Rel->Objects[0] = Adapter->PhysicalDeviceObject;
                ObReferenceObject(Adapter->PhysicalDeviceObject);

                Irp->IoStatus.Information = (ULONG_PTR)Rel;
                Irp->IoStatus.Status      = STATUS_SUCCESS;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SUCCESS;
            }
            else
            {
                /* Other relation types: forward to lower driver. */
                return DxgkpForwardIrp(Adapter, Irp);
            }
        }

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        {
            if (Adapter->MiniportContext != NULL &&
                Adapter->MiniportContext->IsBasicDisplayFallback)
            {
                BOOLEAN Suppressed;

                /* Native dxgkrnl disables its Basic Display fallback in the
                 * adapter itself. Our fallback is a synthetic root device,
                 * whose lower PDO has no hardware state to contribute, so
                 * answer the optional query here. A fresh query starts with
                 * zero flags and therefore also makes rollback visible. */
                Suppressed = (InterlockedCompareExchange(
                                  &Adapter->BasicDisplayUiSuppressed,
                                  0,
                                  0) != 0);
                if (Suppressed)
                {
                    Irp->IoStatus.Information |=
                        PNP_DEVICE_DONT_DISPLAY_IN_UI;
                }
                DXGKRNL_TRACE("BASICDISPLAY_UI: query adapter %p "
                              "suppressed=%u flags=0x%Ix\n",
                              Adapter,
                              Suppressed,
                              Irp->IoStatus.Information);
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SUCCESS;
            }
            return DxgkpForwardIrp(Adapter, Irp);
        }

        case IRP_MN_STOP_DEVICE:
        {
            Status = DxgkAdapterStop(Adapter);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            return DxgkpForwardIrp(Adapter, Irp);
        }

        case IRP_MN_REMOVE_DEVICE:
        {
            PDEVICE_OBJECT LowerDevice = Adapter->LowerDeviceObject;

            if (LowerDevice != NULL)
                ObReferenceObject(LowerDevice);

            /* Release miniport and OS-owned resources while the lower stack
             * still owns live hardware, then forward before detach/delete. */
            DxgkAdapterRemove(Adapter);

            if (LowerDevice != NULL)
            {
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoSkipCurrentIrpStackLocation(Irp);
                Status = IoCallDriver(LowerDevice, Irp);
                DxgkpDeleteRemovedAdapterFdo(Adapter);
                ObDereferenceObject(LowerDevice);
                return Status;
            }

            DxgkpDeleteRemovedAdapterFdo(Adapter);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }

        case IRP_MN_SURPRISE_REMOVAL:
        {
            PDXGKDDI_NOTIFY_SURPRISE_REMOVAL NotifyCallback;
            DXGKRNL_ADAPTER_STATE PreviousState;
            BOOLEAN NotifyRunningRemoval;
            BOOLEAN WaitForStop;
            NTSTATUS NotifyStatus;
            NTSTATUS StopStatus = STATUS_SUCCESS;

            DxgkpAcquireAdapterMutexAfterStart(Adapter);
            PreviousState = Adapter->State;
            WaitForStop = (InterlockedCompareExchange(&Adapter->AdapterStopInProgress, 0, 0) != 0);
            NotifyRunningRemoval = PreviousState == DxgkAdapterStateStarted || PreviousState == DxgkAdapterStateStopping || WaitForStop;
            Adapter->State = DxgkAdapterStateSurpriseRemoved;
            DxgkMarkAdapterDevicesStoppedLocked(Adapter);
            KeReleaseMutex(&Adapter->AdapterMutex, FALSE);

            /* Level-0: notify immediately and deliberately bypass ordinary
             * KMD admission so this can run while another DDI is pending. */
            NotifyCallback = DXGK_CB(Adapter, DxgkDdiNotifySurpriseRemoval);
            if (NotifyRunningRemoval)
            {
                if (!Adapter->SupportSurpriseRemoval)
                    DxgkpBugCheckSurpriseRemoval(Adapter, STATUS_NOT_SUPPORTED, NotifyCallback);
                if (NotifyCallback == NULL || Adapter->MiniportDeviceContext == NULL || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
                    DxgkpBugCheckSurpriseRemoval(Adapter, STATUS_PROCEDURE_NOT_FOUND, NotifyCallback);
                _SEH2_TRY
                {
                    NotifyStatus = NotifyCallback(Adapter->MiniportDeviceContext, DxgkRemovalPnPNotify);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    NotifyStatus = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(NotifyStatus))
                    DxgkpBugCheckSurpriseRemoval(Adapter, NotifyStatus, NotifyCallback);
                Adapter->SurpriseRemovalHandled = TRUE;
            }

            if (!WaitForStop)
            {
                NTSTATUS Mms2Status;

                Mms2Status = DxgkpMms2BeginStop(Adapter, Dxgmms2StopReasonSurpriseRemove);
                if (!NT_SUCCESS(Mms2Status))
                    DXGKRNL_ERR("DxgkpMiniportPnpDispatch: dxgmms2 surprise begin-stop failed 0x%08lX; final RemoveDevice will retry\n", Mms2Status);
            }

            if (WaitForStop)
                StopStatus = DxgkAdapterStop(Adapter);
            if (WaitForStop && NT_SUCCESS(StopStatus))
                return DxgkpForwardIrp(Adapter, Irp);
            InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
            DxgkPresentBeginStop(Adapter);
            if (InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 1, 0) == 0)
                ExWaitForRundownProtectionRelease(&Adapter->RemoveRundownRef);
            DxgkBeginAdapterRundown(Adapter);
            /* A resetting TDR owns Level3 and may still need ISR/DPC progress. */
            DxgkpStopTdrWatchdog(Adapter);
            DxgkpWaitForFlagClear(&Adapter->HotPlugWorkActive);
            ASSERT(DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(&Adapter->HotPlugWorkActive));
            /* Serialize the remaining Level3 teardown against power/query/stop. */
            DxgkAcquireLevel3Transition(Adapter);
            DxgkBeginKmdExclusive(Adapter);
            InterlockedExchange(&Adapter->VidSchStopping, 1);
            DxgkpDisconnectAdapterInterrupt(Adapter);
            DxgkPresentTeardown(Adapter);
            DxgkWaitForSubmitDmaReservations(Adapter);
            VidSchPrepareForStop(Adapter);
            KeRemoveQueueDpc(&Adapter->DpcObject);
            KeFlushQueuedDpcs();
            /* Do not reopen public/KMT admission after hardware removal.
             * RemoveDevice later owns KMD exclusivity for cleanup DDIs. */
            DxgkEndKmdExclusive(Adapter, FALSE);
            DxgkReleaseLevel3Transition(Adapter);
            return DxgkpForwardIrp(Adapter, Irp);
        }

        default:
            return DxgkpForwardIrp(Adapter, Irp);
    }
}

static NTSTATUS
DxgkpForwardPowerIrpSynchronously(
    _In_ PDEVICE_OBJECT LowerDeviceObject,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    PAGED_CODE();
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, DxgkpStartDeviceCompletion, &Event, TRUE, TRUE, TRUE);
    PoStartNextPowerIrp(Irp);
    Status = PoCallDriver(LowerDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static NTSTATUS
DxgkpCallMiniportSetPowerState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DEVICE_POWER_STATE NewState,
    _In_ POWER_ACTION ShutdownType)
{
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(Adapter->KmdExclusiveOwnerThread == PsGetCurrentThread());
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->InitData.s.DxgkDdiSetPowerState == NULL)
        return STATUS_SUCCESS;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DELETE_PENDING;
    Status = STATUS_DELETE_PENDING;
    if (Adapter->State == DxgkAdapterStateStarted && Adapter->MiniportDeviceContext != NULL)
    {
        _SEH2_TRY
        {
            Status = Adapter->MiniportContext->InitData.s.DxgkDdiSetPowerState(Adapter->MiniportDeviceContext, DISPLAY_ADAPTER_HW_ID, NewState, ShutdownType);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    DxgkReleaseMiniportCallback(Adapter);
    return Status;
}

/*
 * DxgkpMiniportPowerDispatch
 *
 * IRP_MJ_POWER handler installed into the miniport DriverObject.
 * Handles device power state changes by calling DxgkDdiSetPowerState
 * with the DISPLAY_ADAPTER_HW_ID device UID (targets the whole GPU).
 *
 * IRQL: PASSIVE_LEVEL for set-power; may be called at DISPATCH_LEVEL
 *       for query-power by some callers — handled by forwarding directly.
 */
NTSTATUS
NTAPI
DxgkpMiniportPowerDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PDXGKRNL_ADAPTER   Adapter;
    PIO_STACK_LOCATION Stack;
    PDEVICE_OBJECT LowerDeviceObject;
    NTSTATUS Status;
    BOOLEAN Level3TransitionHeld = FALSE;

    /*
     * Route non-adapter devices: \Device\Video0, \Device\DxgKrnl, child PDOs.
     */
    if (GDxgControlDeviceObject != NULL && DeviceObject == GDxgControlDeviceObject)
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    {
        PULONG Signature = (PULONG)DeviceObject->DeviceExtension;
        if (Signature != NULL && *Signature == DXGK_CHILD_PDO_SIGNATURE)
        {
            PoStartNextPowerIrp(Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
    }

    /* Check for display device (\Device\Video0) — not a real power device. */
    {
        extern PDEVICE_OBJECT g_DisplayDeviceObject;
        if (g_DisplayDeviceObject != NULL && DeviceObject == g_DisplayDeviceObject)
        {
            PoStartNextPowerIrp(Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
    }

    Adapter = DXGKRNL_ADAPTER_FROM_DEVOBJ(DeviceObject);
    Stack   = IoGetCurrentIrpStackLocation(Irp);

    if (InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || !ExAcquireRundownProtection(&Adapter->RemoveRundownRef))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }
    if (InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->LowerDeviceObject == NULL)
    {
        ExReleaseRundownProtection(&Adapter->RemoveRundownRef);
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }
    LowerDeviceObject = Adapter->LowerDeviceObject;
    ObReferenceObject(LowerDeviceObject);

    if (Stack->MinorFunction == IRP_MN_SET_POWER &&
        Stack->Parameters.Power.Type == DevicePowerState &&
        Adapter->State == DxgkAdapterStateStarted)
    {
        DEVICE_POWER_STATE NewState;
        DEVICE_POWER_STATE CurrentState;
        POWER_ACTION ShutdownType;
        BOOLEAN ValidState;
        BOOLEAN KmdAdmissionBlocked;
        BOOLEAN InterruptAdmissionBlocked;
        BOOLEAN PoweringDown;
        BOOLEAN PoweringUp;

        DxgkAcquireLevel3Transition(Adapter);
        Level3TransitionHeld = TRUE;
        if (Adapter->State != DxgkAdapterStateStarted)
        {
            DxgkReleaseLevel3Transition(Adapter);
            Level3TransitionHeld = FALSE;
            goto ForwardPowerIrp;
        }
        NewState = Stack->Parameters.Power.State.DeviceState;
        CurrentState = Adapter->DevicePowerState;
        ShutdownType = Stack->Parameters.Power.ShutdownType;
        ValidState = NewState >= PowerDeviceD0 && NewState < PowerDeviceMaximum;
        KmdAdmissionBlocked = InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) != 0;
        InterruptAdmissionBlocked = InterlockedCompareExchange(&Adapter->InterruptCallbacksBlocked, 0, 0) != 0;
        PoweringDown = ValidState && NewState > PowerDeviceD0 && (NewState > CurrentState || (NewState == CurrentState && (!KmdAdmissionBlocked || !InterruptAdmissionBlocked)));
        PoweringUp = ValidState && (NewState < CurrentState || (NewState == PowerDeviceD0 && KmdAdmissionBlocked && InterruptAdmissionBlocked));

        DXGKRNL_TRACE("DxgkpMiniportPowerDispatch: SET_POWER D%d -> D%d down=%u up=%u\n", CurrentState - PowerDeviceD0, NewState - PowerDeviceD0, PoweringDown, PoweringUp);

        if (PoweringDown)
        {
            NTSTATUS KmdStatus;
            NTSTATUS LowerStatus;
            NTSTATUS ResumeStatus = STATUS_SUCCESS;
            NTSTATUS SchedulerStatus;
            NTSTATUS TrackerStatus;
            BOOLEAN InterruptCallbacksBlocked = InterruptAdmissionBlocked;
            BOOLEAN LowerPowerIrpStarted = FALSE;
            BOOLEAN SchedulerSuspended;

            InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
            DxgkPresentBeginStop(Adapter);
            DxgkReleaseLevel3Transition(Adapter);
            Level3TransitionHeld = FALSE;
            DxgkpStopTdrWatchdog(Adapter);
            DxgkAcquireLevel3Transition(Adapter);
            Level3TransitionHeld = TRUE;
            if (Adapter->State != DxgkAdapterStateStarted)
            {
                DxgkReleaseLevel3Transition(Adapter);
                Level3TransitionHeld = FALSE;
                goto ForwardPowerIrp;
            }
            DxgkWaitForSubmitDmaReservations(Adapter);
            if (InterlockedCompareExchange(&Adapter->PresentQueueActiveCalls, 0, 0) != 0)
                KeWaitForSingleObject(&Adapter->PresentQueueCallsDrainedEvent, Executive, KernelMode, FALSE, NULL);
            DxgkPresentCancelAllStopped(Adapter);

            SchedulerStatus = VidSchSuspendScheduler(Adapter);
            SchedulerSuspended = NT_SUCCESS(SchedulerStatus) && Adapter->VidSchContext != NULL && CurrentState == PowerDeviceD0;
            if (!NT_SUCCESS(SchedulerStatus) && SchedulerStatus != STATUS_NOT_SUPPORTED)
            {
                if (CurrentState == PowerDeviceD0)
                {
                    DxgkBeginKmdExclusive(Adapter);
                    DxgkVidMmResumeAdapter(Adapter);
                    DxgkUnblockInterruptCallbacks(Adapter);
                    InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
                    DxgkPresentResume(Adapter);
                    DxgkpStartTdrWatchdog(Adapter);
                    DxgkEndKmdExclusive(Adapter, TRUE);
                }
                else
                {
                    DxgkBeginKmdExclusive(Adapter);
                    DxgkVidMmQuiesceAdapter(Adapter);
                    DxgkBlockInterruptCallbacks(Adapter);
                    DxgkEndKmdExclusive(Adapter, FALSE);
                }
                PoStartNextPowerIrp(Irp);
                Status = SchedulerStatus;
                goto CompletePowerIrp;
            }

            TrackerStatus = DxgkpWaitForTrackedDmaIdle(Adapter, 1000);
            if (!NT_SUCCESS(TrackerStatus))
            {
                Status = TrackerStatus;
                DxgkBeginKmdExclusive(Adapter);
                if (CurrentState == PowerDeviceD0)
                {
                    DxgkVidMmResumeAdapter(Adapter);
                    if (SchedulerSuspended)
                        ResumeStatus = VidSchResumeScheduler(Adapter);
                    if (NT_SUCCESS(ResumeStatus) || ResumeStatus == STATUS_NOT_SUPPORTED)
                    {
                        DxgkUnblockInterruptCallbacks(Adapter);
                        InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
                        DxgkPresentResume(Adapter);
                        DxgkpStartTdrWatchdog(Adapter);
                        DxgkEndKmdExclusive(Adapter, TRUE);
                    }
                    else
                    {
                        Status = ResumeStatus;
                        DxgkVidMmQuiesceAdapter(Adapter);
                        DxgkBlockInterruptCallbacks(Adapter);
                        DxgkEndKmdExclusive(Adapter, FALSE);
                    }
                }
                else
                {
                    DxgkVidMmQuiesceAdapter(Adapter);
                    DxgkBlockInterruptCallbacks(Adapter);
                    DxgkEndKmdExclusive(Adapter, FALSE);
                }
                PoStartNextPowerIrp(Irp);
                goto CompletePowerIrp;
            }

            DxgkBeginKmdExclusive(Adapter);
            DxgkVidMmQuiesceAdapter(Adapter);
            /* Paging eviction can require ISR/DPC completion progress. */
            Status = DxgkVidMmPrepareForIdle(Adapter);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkpMiniportPowerDispatch: power-down VidMm idle preparation failed 0x%08lX; restoring D%d\n", Status, CurrentState - PowerDeviceD0);
                goto RollbackPowerDown;
            }
            DxgkBlockInterruptCallbacks(Adapter);
            InterruptCallbacksBlocked = TRUE;
            if (Adapter->State != DxgkAdapterStateStarted || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
            {
                Status = STATUS_DELETE_PENDING;
                goto RetainPowerDownAdmission;
            }
            KmdStatus = DxgkpCallMiniportSetPowerState(Adapter, NewState, ShutdownType);
            if (!NT_SUCCESS(KmdStatus))
            {
                DXGKRNL_WARN("DxgkpMiniportPowerDispatch: power-down DxgkDdiSetPowerState failed 0x%08lX; restoring D%d\n", KmdStatus, CurrentState - PowerDeviceD0);
                Status = KmdStatus;
                goto RollbackPowerDown;
            }

            LowerPowerIrpStarted = TRUE;
            LowerStatus = DxgkpForwardPowerIrpSynchronously(LowerDeviceObject, Irp);
            if (!NT_SUCCESS(LowerStatus))
            {
                KmdStatus = DxgkpCallMiniportSetPowerState(Adapter, CurrentState, PowerActionNone);
                if (!NT_SUCCESS(KmdStatus))
                {
                    DXGKRNL_ERR("DxgkpMiniportPowerDispatch: lower power-down failed 0x%08lX and D%d compensation failed 0x%08lX; retaining blocked admission\n", LowerStatus, CurrentState - PowerDeviceD0, KmdStatus);
                    Status = KmdStatus;
                    goto RetainPowerDownAdmission;
                }
                Status = LowerStatus;
                goto RollbackPowerDown;
            }

            Adapter->DevicePowerState = NewState;
            Status = LowerStatus;
            DxgkEndKmdExclusive(Adapter, FALSE);
            goto CompletePowerIrp;

RollbackPowerDown:
            if (CurrentState != PowerDeviceD0 || Adapter->State != DxgkAdapterStateStarted || Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) != 0 || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
                goto RetainPowerDownAdmission;
            DxgkVidMmResumeAdapter(Adapter);
            if (SchedulerSuspended)
                ResumeStatus = VidSchResumeScheduler(Adapter);
            if (!NT_SUCCESS(ResumeStatus) && ResumeStatus != STATUS_NOT_SUPPORTED)
            {
                DXGKRNL_ERR("DxgkpMiniportPowerDispatch: D0 rollback scheduler resume failed 0x%08lX; retaining blocked admission\n", ResumeStatus);
                DxgkVidMmQuiesceAdapter(Adapter);
                Status = ResumeStatus;
                goto RetainPowerDownAdmission;
            }
            if (InterruptCallbacksBlocked)
                DxgkUnblockInterruptCallbacks(Adapter);
            InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
            DxgkPresentResume(Adapter);
            DxgkpStartTdrWatchdog(Adapter);
            DxgkEndKmdExclusive(Adapter, TRUE);
            if (!LowerPowerIrpStarted)
                PoStartNextPowerIrp(Irp);
            goto CompletePowerIrp;

RetainPowerDownAdmission:
            if (!InterruptCallbacksBlocked)
            {
                DxgkBlockInterruptCallbacks(Adapter);
                InterruptCallbacksBlocked = TRUE;
            }
            DxgkEndKmdExclusive(Adapter, FALSE);
            if (!LowerPowerIrpStarted)
                PoStartNextPowerIrp(Irp);
            goto CompletePowerIrp;
        }

        if (PoweringUp)
        {
            NTSTATUS KmdStatus;
            NTSTATUS LowerStatus;
            NTSTATUS SchedulerStatus;

            InterlockedExchange(&Adapter->SubmitDmaStopping, 1);
            DxgkPresentBeginStop(Adapter);
            InterlockedExchange(&Adapter->KmdCallsBlocked, 1);
            LowerStatus = DxgkpForwardPowerIrpSynchronously(LowerDeviceObject, Irp);
            Status = LowerStatus;
            if (!NT_SUCCESS(LowerStatus))
                goto CompletePowerIrp;

            DxgkBeginKmdExclusive(Adapter);
            KmdStatus = DxgkpCallMiniportSetPowerState(Adapter, NewState, ShutdownType);
            if (!NT_SUCCESS(KmdStatus))
            {
                DXGKRNL_WARN("DxgkpMiniportPowerDispatch: power-up DxgkDdiSetPowerState failed 0x%08lX; completing the lower power IRP and retaining blocked admission\n", KmdStatus);
                Status = KmdStatus;
                DxgkEndKmdExclusive(Adapter, FALSE);
                goto CompletePowerIrp;
            }
            Adapter->DevicePowerState = NewState;

            if (NewState == PowerDeviceD0)
            {
                DxgkVidMmResumeAdapter(Adapter);
                SchedulerStatus = VidSchResumeScheduler(Adapter);
                if (!NT_SUCCESS(SchedulerStatus) && SchedulerStatus != STATUS_NOT_SUPPORTED)
                {
                    DXGKRNL_ERR("DxgkpMiniportPowerDispatch: D0 scheduler resume failed 0x%08lX; retaining blocked admission\n", SchedulerStatus);
                    DxgkVidMmQuiesceAdapter(Adapter);
                    Status = SchedulerStatus;
                    DxgkEndKmdExclusive(Adapter, FALSE);
                    goto CompletePowerIrp;
                }
                DxgkUnblockInterruptCallbacks(Adapter);
                InterlockedExchange(&Adapter->SubmitDmaStopping, 0);
                DxgkPresentResume(Adapter);
                DxgkpStartTdrWatchdog(Adapter);
                DxgkEndKmdExclusive(Adapter, TRUE);
            }
            else
            {
                DxgkEndKmdExclusive(Adapter, FALSE);
            }
            goto CompletePowerIrp;
        }

        DxgkReleaseLevel3Transition(Adapter);
        Level3TransitionHeld = FALSE;
    }

ForwardPowerIrp:
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(LowerDeviceObject, Irp);
    ObDereferenceObject(LowerDeviceObject);
    ExReleaseRundownProtection(&Adapter->RemoveRundownRef);
    return Status;

CompletePowerIrp:
    if (Level3TransitionHeld)
        DxgkReleaseLevel3Transition(Adapter);
    ObDereferenceObject(LowerDeviceObject);
    ExReleaseRundownProtection(&Adapter->RemoveRundownRef);
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static BOOLEAN
DxgkpAcquireMiniportRegistration(
    _In_ PDXGKRNL_MINIPORT_CONTEXT MpCtx)
{
    if (MpCtx == NULL || MpCtx->Signature != DXGKP_MINIPORT_CONTEXT_SIGNATURE || InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationRegistered)
        return FALSE;
    if (!ExAcquireRundownProtection(&MpCtx->RegistrationRundown))
        return FALSE;
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationRegistered)
    {
        ExReleaseRundownProtection(&MpCtx->RegistrationRundown);
        return FALSE;
    }
    return TRUE;
}

static VOID
DxgkpReleaseMiniportRegistration(
    _In_ PDXGKRNL_MINIPORT_CONTEXT MpCtx)
{
    ExReleaseRundownProtection(&MpCtx->RegistrationRundown);
}

static VOID
DxgkpFreeMiniportRegistryPath(
    _Inout_ PDXGKRNL_MINIPORT_CONTEXT MpCtx)
{
    if (MpCtx->RegistryPath.Buffer != NULL)
        ExFreePoolWithTag(MpCtx->RegistryPath.Buffer, TAG_DXGK_REGISTRY);
    RtlZeroMemory(&MpCtx->RegistryPath, sizeof(MpCtx->RegistryPath));
}

static VOID
DxgkpClearMiniportRegistrationPayload(
    _Inout_ PDXGKRNL_MINIPORT_CONTEXT MpCtx)
{
    DxgkpFreeMiniportRegistryPath(MpCtx);
    RtlZeroMemory(&MpCtx->InitData, sizeof(MpCtx->InitData));
    MpCtx->InitDataSize = 0;
    MpCtx->IsDisplayOnlyDriver = FALSE;
    MpCtx->UseDodLayout = FALSE;
    MpCtx->IsBasicDisplayFallback = FALSE;
}

/*
 * DxgkpAddDevice
 *
 * DRIVER_ADD_DEVICE callback installed into the miniport's DriverObject
 * DriverExtension->AddDevice by DxgkInitializeEx.  Called by the PnP
 * manager when it matches a GPU PDO to this miniport.
 *
 * Creates the FDO, calls DxgkDdiAddDevice, attaches to the device stack,
 * and links the adapter into both per-miniport and global lists.
 *
 * IRQL: PASSIVE_LEVEL
 */
static NTSTATUS
DxgkpAddDeviceRegistered(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    PDEVICE_OBJECT            Fdo;
    PDXGKRNL_ADAPTER          Adapter;
    PVOID                     MiniportDeviceContext;
    KIRQL                     OldIrql;
    NTSTATUS                  Mms2Status;
    NTSTATUS                  Status;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkpAddDevice: DriverObject %p PDO %p\n",
                  DriverObject, PhysicalDeviceObject);

    /* Retrieve the per-miniport context from the DriverObjectExtension. */
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)
            IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);
    if (MpCtx == NULL)
    {
        DXGKRNL_ERR("DxgkpAddDevice: no miniport context for %p\n",
                    DriverObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    /* Registration already enforces the Win11 profile.  Keep the same floor
     * at AddDevice so a corrupted or stale registration cannot reintroduce a
     * pre-WDDM2 miniport through the PnP path. */
    if (!DxgkCapsCoreInterfaceVersionAtLeast(
            MpCtx->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
    {
        DXGKRNL_WARN("DxgkpAddDevice: pre-WDDM2 miniport version 0x%lX rejected by the Win11 profile\n", MpCtx->InitData.s.Version);
        return STATUS_NOT_SUPPORTED;
    }

    /* Create the FDO; size the DeviceExtension to hold DXGKRNL_ADAPTER. */
    Status = IoCreateDevice(DriverObject,
                            sizeof(DXGKRNL_ADAPTER),
                            NULL,       /* no device name for FDOs */
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: IoCreateDevice failed 0x%08lX\n",
                    Status);
        return Status;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;

    Adapter = DXGKRNL_ADAPTER_FROM_DEVOBJ(Fdo);
    RtlZeroMemory(Adapter, sizeof(*Adapter));

    Adapter->MiniportContext         = MpCtx;
    Adapter->FunctionalDeviceObject  = Fdo;
    Adapter->PhysicalDeviceObject    = PhysicalDeviceObject;
    Adapter->State                   = DxgkAdapterStateUninitialized;
    Adapter->DevicePowerState        = PowerDeviceD0;
    Adapter->SystemPowerState        = PowerSystemWorking;
    Adapter->HighestAcceptableAddress.QuadPart = (LONGLONG)-1;
    Adapter->SchedulingCaps.Value = 0;
    Adapter->TdrConfig = g_TdrConfig;

    Status = ExUuidCreate(&Adapter->AdapterGuid);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: ExUuidCreate failed 0x%08lX\n", Status);
        IoDeleteDevice(Fdo);
        return Status;
    }

    Status = ZwAllocateLocallyUniqueId(&Adapter->AdapterLuid);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: ZwAllocateLocallyUniqueId failed "
                    "0x%08lX\n", Status);
        IoDeleteDevice(Fdo);
        return Status;
    }

    /* Initialise synchronisation primitives. */
    KeInitializeSpinLock(&Adapter->InterruptLock);
    DxgkPeriodicInterruptCoreInitialize(
        &Adapter->PeriodicInterruptCore);
    KeInitializeSpinLock(&Adapter->ChildListLock);
    KeInitializeMutex(&Adapter->PresentLifecycleMutex, 0);
    KeInitializeMutex(&Adapter->CddPresentMutex, 0);
    KeInitializeSpinLock(&Adapter->SubmitDmaLock);
    KeInitializeSpinLock(&Adapter->DmaBufferCacheLock);
    KeInitializeSpinLock(&Adapter->TdrHistoryLock);
    KeInitializeMutex(&Adapter->AdapterMutex, 0);
    KeInitializeMutex(&Adapter->VidPnMutex, 0);
    KeInitializeMutex(&Adapter->OverlayMutex, 0);
    KeInitializeEvent(&Adapter->AdapterStartCompletedEvent, NotificationEvent, TRUE);
    Adapter->AdapterStartGeneration = 0;
    Adapter->AdapterStartCompletedGeneration = 0;
    Adapter->AdapterStartStatus = STATUS_SUCCESS;
    KeInitializeEvent(&Adapter->AdapterStopCompletedEvent, NotificationEvent, TRUE);
    Adapter->AdapterStopInProgress = 0;
    Adapter->AdapterStopGeneration = 0;
    Adapter->AdapterStopCompletedGeneration = 0;
    Adapter->AdapterStopIntentCount = 0;
    Adapter->AdapterStopStatus = STATUS_SUCCESS;
    Adapter->Mms2ContextStreamValid = 0;
    Adapter->Mms2TimelineCallsOpen = 0;
    Adapter->Mms2TimelineActiveCalls = 0;
    Adapter->SubmittedFenceIdentityEpoch = 1;
    Adapter->SubmittedFenceIdentityResetting = 0;
    KeInitializeMutex(&Adapter->MiniportCallbackMutex, 0);
    KeInitializeMutex(&Adapter->KmdExclusiveMutex, 0);
    KeInitializeMutex(&Adapter->KmdTransactionMutex, 0);
    KeInitializeMutex(&Adapter->Level3TransitionMutex, 0);
    Adapter->KmdCallsBlocked = 0;
    Adapter->KmdActiveCalls = 0;
    Adapter->KmdExclusiveOwnerThread = NULL;
    Adapter->KmdTransactionOwnerThread = NULL;
    Adapter->KmdTransactionDepth = 0;
    Adapter->Level3TransitionOwnerThread = NULL;
    Adapter->Level3TransitionDepth = 0;
    Adapter->InterruptCallbacksBlocked = 1;
    Adapter->InterruptActiveCalls = 0;
    KeInitializeMutex(&Adapter->SharedPrimaryMutex, 0);
    ExInitializeRundownProtection(&Adapter->SharedSurfaceRundown);
    Adapter->SharedSurfaceGeneration = 1;
    Adapter->SharedSurfaceMutationDepth = 0;
    Adapter->SharedSurfaceAvailable = 0;
    ExInitializeRundownProtection(&Adapter->RundownRef);
    Adapter->RundownStarted = 0;
    Adapter->DeviceLifecycleActiveOperations = 0;
    KeInitializeEvent(&Adapter->DeviceLifecycleOperationsDrainedEvent, NotificationEvent, TRUE);
    ExInitializeRundownProtection(&Adapter->RemoveRundownRef);
    Adapter->RemoveRundownStarted = 0;
    ExInitializeRundownProtection(&Adapter->ReverseCallbackRundownRef);
    Adapter->ReverseCallbackRundownStarted = 0;
    Adapter->MiniportRemoveDeviceComplete = FALSE;
    KeInitializeDpc(&Adapter->DpcObject, DxgkpAdapterDpcRoutine, Adapter);
    Adapter->SubmitDmaRetireWorkQueued = 0;
    Adapter->SubmitDmaRetireActiveWorkers = 0;
    ExInitializeWorkItem(&Adapter->SubmitDmaRetireWorkItem, DxgkpRetireSubmittedDmaBuffersWorker, Adapter);
    ExInitializeWorkItem(&Adapter->PowerFStateWorkItem, DxgkpPowerFStateWorker, Adapter);
    KeInitializeEvent(&Adapter->PowerFStateDrainedEvent, NotificationEvent, TRUE);
    Adapter->SubmitDmaStopping = 1;
    Adapter->DmaBufferCacheStopping = 0;
    Adapter->DmaBufferCacheCount = 0;
    Adapter->DmaBufferCacheBytes = 0;
    Adapter->SubmitDmaActiveReservations = 0;
    Adapter->PresentQueueInitializationStatus = STATUS_DEVICE_NOT_READY;
    Adapter->PresentQueueStopping = 1;
    Adapter->VBlankResetActive = 0;
    Adapter->PresentQueueActiveCalls = 0;
    Adapter->VBlankResetGeneration = 0;
    Adapter->VidSchStopping = 1;
    Adapter->VidSchActiveCalls = 0;
    Adapter->VidMmBackingCount = 0;
    Adapter->VidMmDestroyWorkerCount = 0;
    Adapter->VidMmDestroyQueuesBlocked = 1;
    Adapter->HotPlugGeneration = 0;
    Adapter->HotPlugWorkActive = 0;
    Adapter->ChildEnumerationEpoch = 0;
    Adapter->ChildRelationsEnumerated = 0;
    DxgkVidPnInitializeHotPlugWorker(Adapter);
    KeInitializeEvent(&Adapter->SubmitDmaRetireDrainedEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->SubmitDmaReservationsDrainedEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->PresentQueueCallsDrainedEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->VidMmBackingsDrainedEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->VidMmDestroyWorkersDrainedEvent, NotificationEvent, TRUE);

    /* Initialise linked lists. */
    InitializeListHead(&Adapter->DeviceListHead);
    InitializeListHead(&Adapter->ChildListHead);
    InitializeListHead(&Adapter->SubmitDmaListHead);
    InitializeListHead(&Adapter->SubmitDmaRetireListHead);
    InitializeListHead(&Adapter->DmaBufferCacheListHead);
    InitializeListHead(&Adapter->MiniportAdapterListEntry);
    InitializeListHead(&Adapter->GlobalAdapterListEntry);

    Status = DxgkpInitializeAdapterRegistryPath(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: display software-key lookup failed "
                    "0x%08lX\n", Status);
        IoDeleteDevice(Fdo);
        return Status;
    }
    Status = DxgkpMms2CreateAdapter(Adapter, DxgkpMms2GetAdapterFlags(Adapter), &Adapter->Mms2Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: dxgmms2 adapter creation failed 0x%08lX\n", Status);
        DxgkpFreeAdapterRegistryPath(Adapter);
        IoDeleteDevice(Fdo);
        return Status;
    }
    Adapter->Mms2State = DxgkMms2AdapterCreated;

    /* Retain the PDO-owned standard bus interface for CONFIG device-space
     * callbacks.  A root-enumerated/software adapter legitimately has none. */
    Status = DxgkpCapturePciBusInterface(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_TRACE("DxgkpAddDevice: no PCI bus interface (0x%08lX)\n",
                      Status);
    }

    /* Call DxgkDdiAddDevice to obtain the miniport's device context. */
    Status = MpCtx->InitData.s.DxgkDdiAddDevice(PhysicalDeviceObject,
                                               &Adapter->MiniportDeviceContext);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkpAddDevice: DxgkDdiAddDevice failed 0x%08lX\n",
                    Status);
        (VOID)DxgkpCollectAdapterDiagnosticInfo(
                  Adapter,
                  DXGK_DI_ADDDEVICE,
                  FALSE);
        Mms2Status = DxgkpMms2DestroyAdministrativeAdapter(Adapter);
        if (!NT_SUCCESS(Mms2Status))
            DxgkpBugCheckMms2Lifecycle(Adapter, Mms2Status, DXGKP_MMS2_FAILURE_ADD_ROLLBACK);
        DxgkpReleasePciBusInterface(Adapter);
        DxgkpFreeAdapterRegistryPath(Adapter);
        IoDeleteDevice(Fdo);
        return Status;
    }

    InterlockedExchange(&Adapter->MiniportCallbacksValid, 1);

    DXGKRNL_TRACE("DxgkpAddDevice: MiniportDeviceContext = %p\n",
                  Adapter->MiniportDeviceContext);

    /* Attach the FDO to the device stack above the PDO. */
    Adapter->LowerDeviceObject =
        IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (Adapter->LowerDeviceObject == NULL)
    {
        DXGKRNL_ERR("DxgkpAddDevice: IoAttachDeviceToDeviceStack failed\n");
        (VOID)KeWaitForSingleObject(&Adapter->MiniportCallbackMutex, Executive, KernelMode, FALSE, NULL);
        MiniportDeviceContext = Adapter->MiniportDeviceContext;
        InterlockedExchange(&Adapter->MiniportCallbacksValid, 0);
        Adapter->MiniportDeviceContext = NULL;
        Status = DxgkpRemoveMiniportDevice(Adapter, MiniportDeviceContext);
        if (!NT_SUCCESS(Status))
            DXGKRNL_ERR("DxgkpAddDevice: rollback DxgkDdiRemoveDevice failed 0x%08lX\n", Status);
        if (InterlockedCompareExchange(&Adapter->ReverseCallbackRundownStarted, 1, 0) == 0)
            ExWaitForRundownProtectionRelease(&Adapter->ReverseCallbackRundownRef);
        KeReleaseMutex(&Adapter->MiniportCallbackMutex, FALSE);
        DxgkpReleaseMapMemory(Adapter);
        Mms2Status = DxgkpMms2DestroyAdministrativeAdapter(Adapter);
        if (!NT_SUCCESS(Mms2Status))
            DxgkpBugCheckMms2Lifecycle(Adapter, Mms2Status, DXGKP_MMS2_FAILURE_ATTACH_ROLLBACK);
        DxgkpReleasePciBusInterface(Adapter);
        DxgkpFreeAdapterRegistryPath(Adapter);
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    /* Propagate alignment requirement from the lower device. */
    Fdo->AlignmentRequirement = Adapter->LowerDeviceObject->AlignmentRequirement;

    /* Register GUID_DISPLAY_DEVICE_ARRIVAL device interface.
     * The interface is registered against the PDO (not the FDO).
     * It will be enabled in DxgkAdapterStart and disabled in DxgkAdapterRemove. */
    RtlInitUnicodeString(&Adapter->DeviceInterfaceName, NULL);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject,
                                       &GUID_DISPLAY_DEVICE_ARRIVAL,
                                       NULL,
                                       &Adapter->DeviceInterfaceName);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpAddDevice: IoRegisterDeviceInterface failed "
                      "0x%08lX (non-fatal)\n", Status);
        /* Non-fatal: the adapter still functions without PnP notifications. */
        RtlInitUnicodeString(&Adapter->DeviceInterfaceName, NULL);
    }
    else
    {
        DXGKRNL_TRACE("DxgkpAddDevice: registered device interface %wZ\n",
                      &Adapter->DeviceInterfaceName);
    }

    /* Link into per-miniport adapter list. */
    KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
    InsertTailList(&MpCtx->AdapterListHead, &Adapter->MiniportAdapterListEntry);
    MpCtx->AdapterCount++;
    KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);

    /* Link into the global adapter list. */
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    InsertTailList(&DxgkAdapterGlobalListHead, &Adapter->GlobalAdapterListEntry);
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);

    Adapter->State = DxgkAdapterStateStopped;

    /* Clear DO_DEVICE_INITIALIZING so the device can receive IRPs. */
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DXGKRNL_TRACE("DxgkpAddDevice: success — FDO %p Adapter %p\n",
                  Fdo, Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkpAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    NTSTATUS Status;

    PAGED_CODE();
    if (DriverObject == NULL || PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);
    if (!DxgkpAcquireMiniportRegistration(MpCtx))
        return STATUS_DELETE_PENDING;
    Status = DxgkpAddDeviceRegistered(DriverObject, PhysicalDeviceObject);
    DxgkpReleaseMiniportRegistration(MpCtx);
    return Status;
}

/*
 * DxgkpDriverUnload
 *
 * DRIVER_UNLOAD callback installed into the miniport DriverObject.
 * Called when the miniport is being unloaded.  Calls DxgkDdiUnload
 * if provided, then releases the registry path buffer.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
NTAPI
DxgkpDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    PDXGKDDI_UNLOAD UnloadCallback = NULL;
    KIRQL OldIrql;
    BOOLEAN LiveAdapters;

    PAGED_CODE();

    DXGKRNL_TRACE("DxgkpDriverUnload: DriverObject %p\n", DriverObject);

    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)
            IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);

    if (MpCtx == NULL || MpCtx->Signature != DXGKP_MINIPORT_CONTEXT_SIGNATURE)
        return;

    (VOID)KeWaitForSingleObject(&g_MiniportRegistrationMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationRegistered)
    {
        KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
        return;
    }
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationUninitializing);
    KeMemoryBarrier();
    ExWaitForRundownProtectionRelease(&MpCtx->RegistrationRundown);
    KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
    LiveAdapters = MpCtx->AdapterCount != 0 || !IsListEmpty(&MpCtx->AdapterListHead);
    KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);
    if (LiveAdapters)
    {
        ExReInitializeRundownProtection(&MpCtx->RegistrationRundown);
        KeMemoryBarrier();
        InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationRegistered);
        KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
        DXGKRNL_ERR("DxgkpDriverUnload: refusing teardown with live adapters\n");
        return;
    }
    UnloadCallback = MpCtx->UseDodLayout ? MpCtx->InitData.dod.DxgkDdiUnload : MpCtx->InitData.s.DxgkDdiUnload;
    KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
    if (UnloadCallback != NULL)
        UnloadCallback();
    (VOID)KeWaitForSingleObject(&g_MiniportRegistrationMutex, Executive, KernelMode, FALSE, NULL);
    DxgkpClearMiniportRegistrationPayload(MpCtx);
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationEmpty);
    KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
}

NTSTATUS
APIENTRY
DxgkUnInitialize(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    KIRQL OldIrql;
    BOOLEAN LiveAdapters;
    NTSTATUS Status;

    PAGED_CODE();
    if (DriverObject == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&DxgkpInitialized, 0, 0) != 2)
        return STATUS_SUCCESS;
    (VOID)KeWaitForSingleObject(&g_MiniportRegistrationMutex, Executive, KernelMode, FALSE, NULL);
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);
    if (MpCtx == NULL || MpCtx->Signature != DXGKP_MINIPORT_CONTEXT_SIGNATURE || InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationRegistered)
    {
        Status = STATUS_SUCCESS;
        goto UninitializeUnlock;
    }
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationUninitializing);
    KeMemoryBarrier();
    ExWaitForRundownProtectionRelease(&MpCtx->RegistrationRundown);
    KeAcquireSpinLock(&MpCtx->AdapterListLock, &OldIrql);
    LiveAdapters = MpCtx->AdapterCount != 0 || !IsListEmpty(&MpCtx->AdapterListHead);
    KeReleaseSpinLock(&MpCtx->AdapterListLock, OldIrql);
    if (LiveAdapters)
    {
        ExReInitializeRundownProtection(&MpCtx->RegistrationRundown);
        KeMemoryBarrier();
        InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationRegistered);
        DXGKRNL_WARN("DxgkUnInitialize: refusing cleanup because the miniport still owns an adapter\n");
        Status = STATUS_DEVICE_BUSY;
        goto UninitializeUnlock;
    }
    DxgkpClearMiniportRegistrationPayload(MpCtx);
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationEmpty);
    Status = STATUS_SUCCESS;

UninitializeUnlock:
    KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
    return Status;
}

/* ========================================================================
 * DxgkInitializeEx / DxgkInitialize — miniport registration entry points
 * ====================================================================== */

#ifndef REACTOS_WDDM_TARGET_LEVEL
#define REACTOS_WDDM_TARGET_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_0
#endif

/*
 * Highest full DRIVER_INITIALIZATION_DATA tail imported and ABI-checked in
 * this translation unit. Keep this independent from the compile selector:
 * dxgkrnl compiles with the newest audited declaration surface, but must not
 * accept a miniport whose declared table extends beyond the tails actually
 * present here.
 */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_3_2
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_3_1
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_3_0
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_9
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_8
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_7
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_6
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_5
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_4
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_3
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_2
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_1
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
#define DXGKP_FULL_INIT_DATA_MAX_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_0
#else
#error dxgkrnl requires WDDM 2.0 or newer public DDI declarations
#endif

/* Return the append-only prefix that a full-table caller compiled for Version
 * can make readable.  Versions newer than the last locally declared tail are
 * deliberately capped at that tail. */
static ULONG
DxgkpFullInitDataPrefixSize(
    _In_ ULONG Version)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_3_2))
        return DXGKP_FIELD_END(
            DRIVER_INITIALIZATION_DATA,
            DxgkDdiResetDisplayEngine);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_3_1))
        return DXGKP_FIELD_END(
            DRIVER_INITIALIZATION_DATA,
            Reserved4);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_3_0))
        return DXGKP_FIELD_END(
            DRIVER_INITIALIZATION_DATA,
            DxgkDdiCancelFlips);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_9))
        return DXGKP_FIELD_END(
            DRIVER_INITIALIZATION_DATA,
            DxgkDdiSetInterruptTargetPresentId);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_8))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_7))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_6))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved3);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_5))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTrackedWorkloadPowerLevel);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_4))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeHwEngine);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_3))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProtectedSession);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_2))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiGetPostCompositionCaps);
#endif
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_1))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateMonitorLinkInfo);
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
        return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion);
    return DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion);
}

/* KMDDOD has a distinct layout. WDDM 2.0 appends the final public callback;
 * later SDKs add no fields through Windows 11 26100. */
static ULONG
DxgkpDodInitDataPrefixSize(
    _In_ ULONG Version)
{
    (VOID)Version;
    return DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiPowerRuntimeSetDeviceHandle);
}

#ifdef _WIN64
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetDisplayPrivateDriverFormat) == 0x1F0);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryVidPnHWCapability) == 0x238);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 0x298);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiFormatHistoryBuffer) == 0x2C8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion) == 0x340);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateMonitorLinkInfo) == 0x370);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateHwContext) == 0x370);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiGetPostCompositionCaps) == 0x408);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateHwContextState) == 0x408);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateProtectedSession) == 0x410);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProtectedSession) == 0x420);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetSchedulingLogBuffer) == 0x420);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryDiagnosticTypesSupport) == 0x468);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeHwEngine) == 0x480);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSignalMonitoredFence) == 0x480);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTargetAdjustedColorimetry2) == 0x498);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTrackedWorkloadPowerLevel) == 0x4A8);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSaveMemoryForHotUpdate) == 0x4A8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCollectDiagnosticInfo) == 0x4B8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved3) == 0x4C8);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x4C8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x4D0);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetFlipQueueLogBuffer) == 0x4D0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateFlipQueueLog) == 0x4D8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelQueuedFlips) == 0x4E0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x4E8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x4F0);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetAllocationBackingStore) == 0x4F0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateCpuEvent) == 0x4F8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyCpuEvent) == 0x500);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x508);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x510);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateNativeFence) == 0x510);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x558);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x560);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateMemoryBasis) == 0x560);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x600);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x608);
#endif
C_ASSERT(DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 0x148);
C_ASSERT(DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiPowerRuntimeSetDeviceHandle) == 0x150);
#else
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetDisplayPrivateDriverFormat) == 0xF8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryVidPnHWCapability) == 0x11C);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 0x14C);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiFormatHistoryBuffer) == 0x164);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion) == 0x1A0);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateMonitorLinkInfo) == 0x1B8);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateHwContext) == 0x1B8);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiGetPostCompositionCaps) == 0x204);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateHwContextState) == 0x204);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateProtectedSession) == 0x208);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProtectedSession) == 0x210);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetSchedulingLogBuffer) == 0x210);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryDiagnosticTypesSupport) == 0x234);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeHwEngine) == 0x240);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSignalMonitoredFence) == 0x240);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTargetAdjustedColorimetry2) == 0x24C);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTrackedWorkloadPowerLevel) == 0x254);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSaveMemoryForHotUpdate) == 0x254);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCollectDiagnosticInfo) == 0x25C);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved3) == 0x264);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x264);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x268);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetFlipQueueLogBuffer) == 0x268);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateFlipQueueLog) == 0x26C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelQueuedFlips) == 0x270);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x274);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x278);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetAllocationBackingStore) == 0x278);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateCpuEvent) == 0x27C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyCpuEvent) == 0x280);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x284);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x288);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateNativeFence) == 0x288);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x2AC);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x2B0);
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateMemoryBasis) == 0x2B0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x300);
C_ASSERT(DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x304);
#endif
C_ASSERT(DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 0xA4);
C_ASSERT(DXGKP_FIELD_END(KMDDOD_INITIALIZATION_DATA, DxgkDdiPowerRuntimeSetDeviceHandle) == 0xA8);
#endif
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Version) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, Version));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiAddDevice) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiAddDevice));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiStartDevice) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiStartDevice));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiStopDevice) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiStopDevice));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiRemoveDevice) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiRemoveDevice));
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUnload) == FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiUnload));

/*
 * DxgkpInitializeMiniport
 *
 * Called from the miniport's DriverEntry.  Validates the DDI callback table,
 * allocates a DXGKRNL_MINIPORT_CONTEXT as a DriverObjectExtension, copies
 * the callback table, saves a canonical copy of the registry path, and hooks
 * the miniport DriverObject.
 *
 * Parameters:
 *   DriverObject              — miniport's DriverObject (from DriverEntry).
 *   RegistryPath              — miniport's registry path (from DriverEntry).
 *   DriverInitDataSize        — byte size of *DriverInitializationData as
 *                               provided by the miniport.
 *   DriverInitializationData  — miniport DDI callback table.
 *
 * Returns:
 *   STATUS_SUCCESS on success.
 *   STATUS_INVALID_PARAMETER if validation fails.
 *   NTSTATUS error from IoAllocateDriverObjectExtension on allocation failure.
 *
 * IRQL: PASSIVE_LEVEL
 */
static NTSTATUS
DxgkpInitializeMiniport(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ ULONG                       DriverInitDataSize,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData,
    _In_ BOOLEAN                     UseDodLayout)
{
    PDXGKRNL_MINIPORT_CONTEXT MpCtx;
    ULONG                     RequiredPrefixSize;
    ULONG                     CopySize;
    ULONG                     Version;
    ULONG                     VersionLevel;
    PWCH                      RegBuf;
    NTSTATUS                  Status;
    BOOLEAN                   NewContext;

    PAGED_CODE();

    /* --- Validate parameters -------------------------------------------- */

    if (DriverObject == NULL ||
        RegistryPath == NULL ||
        DriverInitializationData == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((RegistryPath->Length & (sizeof(WCHAR) - 1)) != 0 || RegistryPath->Length > UNICODE_STRING_MAX_BYTES - sizeof(WCHAR) || RegistryPath->MaximumLength < RegistryPath->Length || (RegistryPath->Length != 0 && RegistryPath->Buffer == NULL))
        return STATUS_INVALID_PARAMETER;

    if (DriverInitDataSize < DXGKP_FIELD_END(DRIVER_INITIALIZATION_DATA, Version))
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: DriverInitDataSize %lu too small\n",
                    DriverInitDataSize);
        return STATUS_INVALID_PARAMETER;
    }

    Version = DriverInitializationData->Version;
    DXGKRNL_TRACE("DxgkpInitializeMiniport: DriverObject %p RegPath %wZ Size=%lu Version=0x%lX Layout=%s\n", DriverObject, RegistryPath, DriverInitDataSize, Version, UseDodLayout ? "DOD" : "full");

    VersionLevel = DxgkCapsCoreInterfaceVersionToLevel(Version);
    if (VersionLevel == 0)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: unknown DDI selector 0x%lX\n",
                    Version);
        return STATUS_REVISION_MISMATCH;
    }
    if (!DxgkCapsCoreInterfaceVersionPermitted(
            Version, REACTOS_WDDM_TARGET_LEVEL))
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: DDI selector 0x%lX level %lu is outside the Win11 miniport range %lu..%lu\n",
                    Version, VersionLevel, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_2_0, (ULONG)REACTOS_WDDM_TARGET_LEVEL);
        return STATUS_REVISION_MISMATCH;
    }

    if (UseDodLayout)
    {
        if (!DxgkCapsCoreInterfaceVersionAtLeast(
                Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
        {
            DXGKRNL_ERR("DxgkpInitializeMiniport: pre-WDDM 2 DOD version 0x%lX is outside the Win11 profile\n", Version);
            return STATUS_INVALID_PARAMETER;
        }
        RequiredPrefixSize = DxgkpDodInitDataPrefixSize(Version);
    }
    else
    {
        if (VersionLevel > DXGKP_FULL_INIT_DATA_MAX_LEVEL)
        {
            DXGKRNL_ERR("DxgkpInitializeMiniport: DDI selector 0x%lX level %lu exceeds imported full-table ceiling %lu\n",
                        Version, VersionLevel,
                        (ULONG)DXGKP_FULL_INIT_DATA_MAX_LEVEL);
            return STATUS_REVISION_MISMATCH;
        }
        if (!DxgkCapsCoreInterfaceVersionAtLeast(
                Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
        {
            DXGKRNL_ERR("DxgkpInitializeMiniport: pre-WDDM 2 full-table version 0x%lX is outside the Win11 profile\n", Version);
            return STATUS_INVALID_PARAMETER;
        }
        RequiredPrefixSize = DxgkpFullInitDataPrefixSize(Version);
    }

    if (DriverInitDataSize < RequiredPrefixSize)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: %s table size %lu is smaller than version 0x%lX prefix %lu\n", UseDodLayout ? "DOD" : "full", DriverInitDataSize, Version, RequiredPrefixSize);
        return STATUS_INVALID_PARAMETER;
    }

    /* The four PnP lifecycle callbacks are required as one transaction. */
    if (DriverInitializationData->DxgkDdiAddDevice == NULL ||
        DriverInitializationData->DxgkDdiStartDevice == NULL ||
        DriverInitializationData->DxgkDdiStopDevice == NULL ||
        DriverInitializationData->DxgkDdiRemoveDevice == NULL)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: mandatory lifecycle callback missing (AddDevice=%p StartDevice=%p StopDevice=%p RemoveDevice=%p)\n",
                    DriverInitializationData->DxgkDdiAddDevice,
                    DriverInitializationData->DxgkDdiStartDevice,
                    DriverInitializationData->DxgkDdiStopDevice,
                    DriverInitializationData->DxgkDdiRemoveDevice);
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * WDDM 2.3 replaces CommitVidPn with SetTimingsFromVidPn for full
     * display miniports. Render-only full-table drivers have no CommitVidPn
     * surface and are therefore not forced into a display contract.
     */
    if (!UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_3) &&
        DriverInitializationData->DxgkDdiCommitVidPn != NULL &&
        DriverInitializationData->DxgkDdiSetTimingsFromVidPn == NULL)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: WDDM 2.3 display miniport "
                    "is missing mandatory SetTimingsFromVidPn\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * CollectDiagnosticInfo is a WDDM 2.6 core requirement for full graphics
     * miniports. Its AddDevice failure form explicitly permits a NULL adapter
     * context, so registration must establish the callback before any PDO is
     * handed to the driver.
     */
    if (!UseDodLayout &&
        DxgkCapsCoreInterfaceVersionAtLeast(
            Version, DXGK_CAPS_CORE_LEVEL_WDDM_2_6) &&
        DriverInitializationData->DxgkDdiCollectDiagnosticInfo == NULL)
    {
        DXGKRNL_ERR("DxgkpInitializeMiniport: WDDM 2.6 miniport "
                    "is missing mandatory CollectDiagnosticInfo\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* --- One-time global init ------------------------------------------- */

    Status = DxgkpEnsureGlobalInitialization();
    if (!NT_SUCCESS(Status))
        return Status;

    /* Import-only callers create a stable dxgkrnl-owned DriverObject rather
     * than binding the global control device to the first miniport. */
    Status = DxgkpEnsureControlDevice();
    if (!NT_SUCCESS(Status) || GDxgControlDeviceObject == NULL)
    {
        DXGKRNL_ERR("DxgkInitializeEx: dxgkrnl control device initialization failed 0x%08lX\n", Status);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_NOT_READY : Status;
    }

    /* The extension is I/O-manager-owned and cannot be deleted separately
     * from the DRIVER_OBJECT.  Serialize allocation/reuse so failed
     * registration and DxgkUnInitialize can return it to an empty state. */
    (VOID)KeWaitForSingleObject(&g_MiniportRegistrationMutex, Executive, KernelMode, FALSE, NULL);
    NewContext = FALSE;
    MpCtx = (PDXGKRNL_MINIPORT_CONTEXT)IoGetDriverObjectExtension(DriverObject, &g_MiniportContextClientId);
    if (MpCtx == NULL)
    {
        Status = IoAllocateDriverObjectExtension(DriverObject, &g_MiniportContextClientId, sizeof(DXGKRNL_MINIPORT_CONTEXT), (PVOID *)&MpCtx);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkInitializeEx: IoAllocateDriverObjectExtension failed 0x%08lX\n", Status);
            goto RegistrationUnlock;
        }
        RtlZeroMemory(MpCtx, sizeof(*MpCtx));
        MpCtx->Signature = DXGKP_MINIPORT_CONTEXT_SIGNATURE;
        KeInitializeSpinLock(&MpCtx->AdapterListLock);
        InitializeListHead(&MpCtx->AdapterListHead);
        ExInitializeRundownProtection(&MpCtx->RegistrationRundown);
        NewContext = TRUE;
    }
    else
    {
        if (MpCtx->Signature != DXGKP_MINIPORT_CONTEXT_SIGNATURE || InterlockedCompareExchange(&MpCtx->RegistrationState, 0, 0) != DxgkMiniportRegistrationEmpty || MpCtx->AdapterCount != 0 || !IsListEmpty(&MpCtx->AdapterListHead))
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
            goto RegistrationUnlock;
        }
        ExReInitializeRundownProtection(&MpCtx->RegistrationRundown);
    }
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationInitializing);

    /* --- Copy callback table -------------------------------------------- */

    /*
     * Copy only the prefix implied by the declared version.  This is the only
     * byte count the size-less wrappers can prove readable.  Explicit callers
     * may provide a larger future table, but unknown append-only tails remain
     * zero in our context and are never read from the caller.
     */
    CopySize = min(RequiredPrefixSize, (ULONG)sizeof(MpCtx->InitData));
    RtlCopyMemory(&MpCtx->InitData, DriverInitializationData, CopySize);
    MpCtx->InitDataSize = CopySize;
    MpCtx->IsDisplayOnlyDriver = UseDodLayout;
    MpCtx->UseDodLayout = UseDodLayout;

    /* --- Copy registry path -------------------------------------------- */

    /*
     * Allocate a non-paged buffer for the registry path and copy it.
     * Length is in bytes (as always for UNICODE_STRING); add 2 for NUL.
     */
    RegBuf = (PWCH)ExAllocatePoolWithTag(
                 NonPagedPool,
                 RegistryPath->Length + sizeof(WCHAR),
                 TAG_DXGK_REGISTRY);
    if (RegBuf == NULL)
    {
        DXGKRNL_ERR("DxgkInitializeEx: registry path alloc failed (%u bytes)\n", RegistryPath->Length + (ULONG)sizeof(WCHAR));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        DxgkpClearMiniportRegistrationPayload(MpCtx);
        ExWaitForRundownProtectionRelease(&MpCtx->RegistrationRundown);
        InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationEmpty);
        goto RegistrationUnlock;
    }

    if (RegistryPath->Length != 0)
        RtlCopyMemory(RegBuf, RegistryPath->Buffer, RegistryPath->Length);
    RegBuf[RegistryPath->Length / sizeof(WCHAR)] = L'\0';

    MpCtx->RegistryPath.Buffer        = RegBuf;
    MpCtx->RegistryPath.Length        = RegistryPath->Length;
    MpCtx->RegistryPath.MaximumLength = RegistryPath->Length + sizeof(WCHAR);

    /*
     * Recognize the in-box Basic Display fallback by its service
     * key name, the same way Windows dxgkrnl special-cases MSBDD.  The
     * fallback yields the boot display to any real miniport that acquires
     * POST display ownership.
     */
    {
        UNICODE_STRING ServiceName;
        ULONG CharIndex = RegistryPath->Length / sizeof(WCHAR);

        while (CharIndex > 0 && RegBuf[CharIndex - 1] != L'\\')
            CharIndex--;

        RtlInitUnicodeString(&ServiceName, &RegBuf[CharIndex]);

        if (ServiceName.Length > 0)
        {
            UNICODE_STRING FallbackName = RTL_CONSTANT_STRING(L"BasicDisplay");

            if (RtlEqualUnicodeString(&ServiceName, &FallbackName, TRUE))
            {
                MpCtx->IsBasicDisplayFallback = TRUE;
                DXGKRNL_TRACE("DxgkInitializeEx: basic-display fallback "
                              "miniport (%wZ)\n", &ServiceName);
            }
        }
    }

    /* --- Hook the miniport DriverObject --------------------------------- */

    DriverObject->DriverExtension->AddDevice           = DxgkpAddDevice;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DxgkDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DxgkDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DxgkDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = DxgkDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP]            = DxgkpMiniportPnpDispatch;
    DriverObject->MajorFunction[IRP_MJ_POWER]          = DxgkpMiniportPowerDispatch;
    DriverObject->DriverUnload                         = DxgkpDriverUnload;

    KeMemoryBarrier();
    InterlockedExchange(&MpCtx->RegistrationState, DxgkMiniportRegistrationRegistered);
    Status = STATUS_SUCCESS;
    DXGKRNL_TRACE("DxgkInitializeEx: success — MpCtx %p Version=0x%lX DOD=%d NewContext=%d\n", MpCtx, MpCtx->InitData.s.Version, MpCtx->IsDisplayOnlyDriver, NewContext);

RegistrationUnlock:
    KeReleaseMutex(&g_MiniportRegistrationMutex, FALSE);
    return Status;
}

/* Explicit-size registration is always the full DRIVER_INITIALIZATION_DATA
 * contract.  The distinct KMDDOD layout enters only through
 * DxgkInitializeDisplayOnlyDriver and is never inferred from a byte count. */
NTSTATUS
APIENTRY
DxgkInitializeEx(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ ULONG                       DriverInitDataSize,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData)
{
    return DxgkpInitializeMiniport(DriverObject, RegistryPath, DriverInitDataSize, DriverInitializationData, FALSE);
}

/*
 * DxgkInitialize
 *
 * Size-less wrapper around DxgkInitializeEx.  Derives the readable append-only
 * prefix from the caller's declared DDI version instead of using dxgkrnl's
 * compile-time structure size.
 * Reached through the public displib entry, which resolves this ReactOS-owned
 * registration target through the dxgkrnl control-device protocol.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData)
{
    if (DriverInitializationData == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkInitializeEx(DriverObject, RegistryPath, DxgkpFullInitDataPrefixSize(DriverInitializationData->Version), DriverInitializationData);
}

/*
 * DxgkInitializeDisplayOnlyDriver
 *
 * Entry point for WDDM Display-Only Drivers (DOD).  The KMDDOD_INITIALIZATION_DATA
 * is a subset of DRIVER_INITIALIZATION_DATA with only DOD-relevant callbacks.
 * Pass the layout explicitly to the shared registration implementation.
 *
 * The KMDDOD_INITIALIZATION_DATA struct has the same leading fields as
 * DRIVER_INITIALIZATION_DATA (Version, AddDevice, StartDevice, etc.) so
 * casting is safe for the subset of callbacks DOD drivers provide.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
DxgkInitializeDisplayOnlyDriver(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PKMDDOD_INITIALIZATION_DATA KmDodInitData)
{
    DXGKRNL_TRACE("DxgkInitializeDisplayOnlyDriver: DriverObject %p Version=0x%lX\n",
                  DriverObject, KmDodInitData ? KmDodInitData->Version : 0);

    if (KmDodInitData == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkpInitializeMiniport(DriverObject, RegistryPath, DxgkpDodInitDataPrefixSize(KmDodInitData->Version), (PDRIVER_INITIALIZATION_DATA)(PVOID)KmDodInitData, TRUE);
}

/* EOF */
