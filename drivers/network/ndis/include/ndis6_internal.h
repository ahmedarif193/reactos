/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/include/ndis6_internal.h
 * PURPOSE:     Internal NDIS 6 bridge data structures.
 *
 *              Shared header for the NDIS 6 implementation files
 *              (60driver.c, 60adapter.c, 60nbl.c, 60io.c, 60thunk.c,
 *              60oid.c, 60bind.c). Each of those files is compiled
 *              with NDIS689_MINIPORT and SKIP_PRECOMPILE_HEADERS ON.
 *              The legacy NDIS 5 sources never include this header,
 *              so it can declare NDIS 6 types freely.
 *
 *              Created on the dev-nt6-1 branch as part of the NDIS
 *              5↔6 bridge work that lets e1000e (NDIS 6.20) carry
 *              traffic for tcpip.sys (NDIS 5.0).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#ifndef _NDIS6_INTERNAL_H_
#define _NDIS6_INTERNAL_H_

/* This file must NOT be included by the legacy 5.x sources because the
 * PCH is built at NDIS 5.1 level and ndis.h gates NDIS 6 types behind
 * NDIS_SUPPORT_NDIS6. The 60*.c files override the version locally. */

#define NDIS689          1
#define NDIS689_MINIPORT 1

#include <ntifs.h>
#include <ndis.h>
#include "miniport.h"
#include "protocol.h"

/* ============================================================================
 *  Private NDIS/WDF class-extension ABI used by NetAdapterCx.
 *
 *  These layouts are not published in the WDK. The definitions below match
 *  the Windows 11 22000 NetAdapterCx PDB ABI used by NetAdapterCx 2.2.
 * ============================================================================ */

typedef NTSTATUS
(NTAPI *PNDIS6_WDF_CX_POWER_REFERENCE)(
    _In_ NDIS_HANDLE AdapterContext,
    _In_ BOOLEAN WaitForD0,
    _In_ BOOLEAN InvokeCompletionCallback);

typedef VOID
(NTAPI *PNDIS6_WDF_CX_ADAPTER_CALLBACK)(
    _In_ NDIS_HANDLE AdapterContext);

typedef VOID
(NTAPI *PNDIS6_WDF_CX_UPDATE_IDLE_CONDITION)(
    _In_ NDIS_HANDLE AdapterContext,
    _In_ ULONG IdleCondition);

typedef PDEVICE_OBJECT
(NTAPI *PNDIS6_WDF_CX_GET_DEVICE_OBJECT)(
    _In_ NDIS_HANDLE AdapterContext);

typedef NTSTATUS
(NTAPI *PNDIS6_WDF_CX_GET_ASSIGNED_FDO_NAME)(
    _In_ NDIS_HANDLE AdapterContext,
    _Inout_ PUNICODE_STRING FdoName);

typedef NDIS_HANDLE
(NTAPI *PNDIS6_WDF_CX_GET_NDIS_HANDLE)(
    _In_ PDEVICE_OBJECT DeviceObject);

typedef VOID
(NTAPI *PNDIS6_WDF_CX_UPDATE_PM_PARAMETERS)(
    _In_ NDIS_HANDLE AdapterContext,
    _In_ PVOID PmParameters);

typedef NTSTATUS
(NTAPI *PNDIS6_WDF_CX_ALLOCATE_MINIPORT_BLOCK)(
    _In_ NDIS_HANDLE AdapterContext,
    _In_ ULONG Size,
    _Out_ PVOID* MiniportBlock);

/* EXECUTION_CONTEXT_FLAGS from NetAdapterCx netcx/ec/inc/executioncontext.h */
#define NDIS6_WDF_EC_FLAG_RUN_DPC_FOR_FIRST_LOOP           0x00000001
#define NDIS6_WDF_EC_FLAG_RUN_WORKER_THREAD_AT_DISPATCH    0x00000002
#define NDIS6_WDF_EC_FLAG_TRY_EXTEND_MAX_TIME_AT_DISPATCH  0x00000004

typedef struct _NDIS6_WDF_EC_WORK_UNIT_KNOBS
{
    ULONG AtPassive;
    ULONG AtDispatch;
} NDIS6_WDF_EC_WORK_UNIT_KNOBS;

/* EXECUTION_CONTEXT_RUNTIME_KNOBS: NetAdapterCx keeps the pointer for the
 * lifetime of the adapter and reads the tunables from it at run time, so
 * the storage backing it must be persistent. */
typedef struct _NDIS6_WDF_EC_RUNTIME_KNOBS
{
    ULONG Size;                                             /* 0x00 */
    ULONG Flags;                                            /* 0x04 */
    ULONG MaxTimeAtDispatch;                                /* 0x08 */
    ULONG DispatchTimeWarning;                              /* 0x0C */
    ULONG DispatchTimeWarningInterval;                      /* 0x10 */
    ULONG DpcWatchdogTimerThreshold;                        /* 0x14 */
    ULONG WorkerThreadPriority;                             /* 0x18 */
    NDIS6_WDF_EC_WORK_UNIT_KNOBS MaxPacketsSend;            /* 0x1C */
    NDIS6_WDF_EC_WORK_UNIT_KNOBS MaxPacketsSendComplete;    /* 0x24 */
    NDIS6_WDF_EC_WORK_UNIT_KNOBS MaxPacketsReceive;         /* 0x2C */
    NDIS6_WDF_EC_WORK_UNIT_KNOBS MaxPacketsReceiveComplete; /* 0x34 */
} NDIS6_WDF_EC_RUNTIME_KNOBS, *PNDIS6_WDF_EC_RUNTIME_KNOBS; /* 0x3C */

typedef struct _NDIS6_WDF_COMPLETE_ADD_PARAMS
{
    GUID InterfaceGuid;
    NET_LUID NetLuid;
    NDIS_MEDIUM MediaType;
    UNICODE_STRING BaseName;
    UNICODE_STRING AdapterInstanceName;
    UNICODE_STRING DriverImageName;
    PNDIS6_WDF_EC_RUNTIME_KNOBS ExecutionContextKnobs;
} NDIS6_WDF_COMPLETE_ADD_PARAMS, *PNDIS6_WDF_COMPLETE_ADD_PARAMS;

typedef VOID
(NTAPI *PNDIS6_WDF_CX_MINIPORT_COMPLETE_ADD)(
    _In_ NDIS_HANDLE AdapterContext,
    _In_ PNDIS6_WDF_COMPLETE_ADD_PARAMS Parameters);

typedef NTSTATUS
(NTAPI *PNDIS6_WDF_CX_DEVICE_RESET)(
    _In_ NDIS_HANDLE AdapterContext,
    _In_ ULONG ResetType);

typedef NTSTATUS
(NTAPI *PNDIS6_WDF_CX_QUERY_DEVICE_RESET_SUPPORT)(
    _In_ NDIS_HANDLE AdapterContext,
    _Out_ PULONG SupportedResetTypes);

typedef NTSTATUS
(NTAPI *PNDIS6_WDF_CX_GET_WMI_EVENT_GUID)(
    _In_ NDIS_HANDLE AdapterContext,
    _In_ NTSTATUS GuidStatus,
    _Out_ PNDIS_GUID* Guid);

typedef struct _NDIS6_WDF_CX_CHARACTERISTICS
{
    NDIS_OBJECT_HEADER Header;
    PNDIS6_WDF_CX_POWER_REFERENCE EvtCxPowerReference;
    PNDIS6_WDF_CX_ADAPTER_CALLBACK EvtCxPowerDereference;
    PNDIS6_WDF_CX_UPDATE_IDLE_CONDITION EvtCxUpdateIdleCondition;
    PNDIS6_WDF_CX_GET_DEVICE_OBJECT EvtCxGetDeviceObject;
    PNDIS6_WDF_CX_GET_DEVICE_OBJECT EvtCxGetNextDeviceObject;
    PNDIS6_WDF_CX_GET_ASSIGNED_FDO_NAME EvtCxGetAssignedFdoName;
    PNDIS6_WDF_CX_GET_NDIS_HANDLE EvtCxGetNdisHandleFromDeviceObject;
    PNDIS6_WDF_CX_UPDATE_PM_PARAMETERS EvtCxUpdatePMParameters;
    PNDIS6_WDF_CX_ALLOCATE_MINIPORT_BLOCK EvtCxAllocateMiniportBlock;
    PNDIS6_WDF_CX_MINIPORT_COMPLETE_ADD EvtCxMiniportCompleteAdd;
    PNDIS6_WDF_CX_ADAPTER_CALLBACK EvtCxDeviceStartComplete;
    PNDIS6_WDF_CX_DEVICE_RESET EvtCxMiniportDeviceReset;
    PNDIS6_WDF_CX_QUERY_DEVICE_RESET_SUPPORT EvtCxMiniportQueryDeviceResetSupport;
    PNDIS6_WDF_CX_GET_WMI_EVENT_GUID EvtCxGetWmiEventGuid;
} NDIS6_WDF_CX_CHARACTERISTICS, *PNDIS6_WDF_CX_CHARACTERISTICS;

typedef struct _NDIS6_WDF_ADD_DEVICE_INFO
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    NDIS_HANDLE MiniportAdapterContext;
    BOOLEAN WdfCxPowerManagement;
} NDIS6_WDF_ADD_DEVICE_INFO, *PNDIS6_WDF_ADD_DEVICE_INFO;

typedef struct _NDIS6_WDF_CX_DRIVER
{
    ULONG Signature;
    PDRIVER_OBJECT DriverObject;
    PVOID DriverContext;
    NDIS6_WDF_CX_CHARACTERISTICS Characteristics;
    volatile LONG ClientCount;
    volatile LONG Deregistering;
} NDIS6_WDF_CX_DRIVER, *PNDIS6_WDF_CX_DRIVER;

#define NDIS6_WDF_CX_SIGNATURE 'xCWN'

/* ============================================================================
 *  NDIS 6 driver block — one per NdisMRegisterMiniportDriver caller
 * ============================================================================ */

typedef struct _NDIS6_DRIVER_BLOCK
{
    ULONG                                   Signature;
    LIST_ENTRY                              ListEntry;
    PDRIVER_OBJECT                          DriverObject;
    UNICODE_STRING                          RegistryPath;
    PVOID                                   MiniportDriverContext;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS    Characteristics;
    NDIS_MINIPORT_PNP_CHARACTERISTICS       PnpCharacteristics;
    BOOLEAN                                 PnpCharacteristicsValid;
#if NDIS_SUPPORT_NDIS630
    NDIS_MINIPORT_SS_CHARACTERISTICS        SelectiveSuspendCharacteristics;
    BOOLEAN                                 SelectiveSuspendCharacteristicsValid;
#endif

    /* Original IRP_MJ_PNP and AddDevice the driver had before we replaced them */
    PDRIVER_DISPATCH                        OriginalPnpDispatch;
    PDRIVER_ADD_DEVICE                      OriginalAddDevice;

    /* WdfDriverCreate installed its own AddDevice + dispatch table before
     * NdisMRegisterMiniportDriver ran: chain AddDevice to KMDF and demux
     * IRPs for KMDF-owned device objects to the saved original dispatch. */
    BOOLEAN                                 IsWdfHybrid;
    PDRIVER_DISPATCH                        OriginalMajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];

    /* NetAdapterCx uses NdisWdfRegisterMiniportDriver and leaves the WDF
     * device stack entirely under WDF ownership. */
    BOOLEAN                                 IsWdfManaged;
    PNDIS6_WDF_CX_DRIVER                    WdfCxDriver;
} NDIS6_DRIVER_BLOCK, *PNDIS6_DRIVER_BLOCK;

#define NDIS6_DRIVER_BLOCK_SIGNATURE 'dMNn'

extern LIST_ENTRY g_Ndis6DriverList;
extern KSPIN_LOCK g_Ndis6DriverListLock;

#define NDIS6_ATTR_TAG 'aANn' /* "nNAa" */

/* ============================================================================
 *  NDIS 6 adapter extension — one per device instance the bridge owns
 * ============================================================================ */

typedef struct _NDIS6_ADAPTER_EXT
{
    /* Back-pointer to the LOGICAL_ADAPTER that owns this extension. */
    PLOGICAL_ADAPTER                Adapter;

    /* Stable interface identity allocated before MiniportInitializeEx. The
     * same values are exposed to the miniport, protocols, filters, and
     * NetAdapterCx for the complete lifetime of this adapter instance. */
    NET_IFINDEX                     IfIndex;
    NET_LUID                        NetLuid;

    /* Pins the LOGICAL_ADAPTER while registration-time snapshots run outside
     * AdapterListLock. Final removal unlinks the adapter first, then drains
     * this rundown before invoking callbacks or freeing either object. */
    EX_RUNDOWN_REF                  LifecycleRundown;

    /* The driver block that registered this miniport. */
    PNDIS6_DRIVER_BLOCK             DriverBlock;

    /* NetAdapterCx miniports live in a WDF object context. NDIS owns only
     * the logical adapter state, not the FDO or its attachment. */
    BOOLEAN                         IsWdfManaged;
    BOOLEAN                         WdfBindingsStarted;
    BOOLEAN                         WdfSurpriseRemoved;
    volatile LONG                   WdfRemoving;
    volatile LONG                   WdfReferenceCount;
    KSPIN_LOCK                      WdfReferenceLock;
    KEVENT                          WdfReferenceDrainEvent;

    /* Driver-supplied per-adapter context, returned via NdisMSetMiniportAttributes
     * and passed to every subsequent callback into the driver. */
    NDIS_HANDLE                     MiniportAdapterContext;

    /* Saved copies of the attribute objects the driver passed in. We keep
     * the bytes around so we can answer cached OID queries without round-
     * tripping to the driver. */
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES   RegistrationAttrs;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES        GeneralAttrs;
    BOOLEAN                                         RegistrationAttrsValid;
    BOOLEAN                                         GeneralAttrsValid;

    /* Set to TRUE after MiniportInitializeEx returns SUCCESS. Cleared
     * before HaltEx is called. If init fails, the driver cleans up its
     * own state internally per MS DDK contract, and HaltEx MUST NOT be
     * invoked — this flag gates that. */
    BOOLEAN                                         Initialized;
    BOOLEAN                                         SurpriseRemoved;

    /* Optional MiniportAddDevice state and the persistent context registered
     * from that callback. This context survives halt/reinitialize cycles and
     * is released only by the matching MiniportRemoveDevice callback. */
    volatile LONG                                   MiniportAddDeviceState;
    NDIS_HANDLE                                     MiniportAddDeviceContext;
    BOOLEAN                                         MiniportAddDeviceAttributesValid;

    /* PnP devnode this adapter is bound to. */
    PDEVICE_OBJECT                  PhysicalDeviceObject;
    PDEVICE_OBJECT                  FunctionalDeviceObject;

    /* Hardware resources extracted from IRP_MN_START_DEVICE. */
    PVOID                           MmioBase;       /* mapped BAR */
    ULONG                           MmioSize;
    ULONG                           IoPortBase;     /* IO BAR */
    ULONG                           IoPortLength;
    ULONG                           InterruptVector;
    KIRQL                           InterruptIrql;
    KAFFINITY                       InterruptAffinity;
    ULONG                           InterruptFlags; /* CM_RESOURCE_INTERRUPT_* */

    /* Full resource lists from PnP. We pass these straight through to the
     * driver via NDIS_MINIPORT_INIT_PARAMETERS so the driver's hardware-
     * setup code can find the BARs and IRQs itself. The bridge owns the
     * memory until the adapter is destroyed. */
    PCM_RESOURCE_LIST               AllocatedResources;
    PCM_RESOURCE_LIST               AllocatedResourcesTranslated;

    /* Interrupt registration. Wired in Phase 2:
     *   NdisMRegisterInterruptEx -> IoConnectInterrupt with Ndis6IsrWrapper.
     *   InterruptHandler runs at DIRQL, queues InterruptDpc which then calls
     *   the miniport's InterruptDpcHandler at DISPATCH_LEVEL. */
    PKINTERRUPT                     InterruptObject;
    KSPIN_LOCK                      IsrLock;
    KDPC                            InterruptDpc;
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS IntChars;
    NDIS_HANDLE                     MiniportInterruptContext;
    /* High bit closes the gate; low bits count queued/running ISR-DPC work. */
    volatile LONG                   InterruptRundownState;
    KEVENT                          InterruptDrainEvent;

    /* C1: MSI/MSI-X support. If NdisMRegisterInterruptEx requested a
     * message-based connection, we used IoConnectInterruptEx which
     * returned an IO_INTERRUPT_MESSAGE_INFO table with one entry per
     * message vector. MsiTable is that pointer (or NULL for line-based). */
    PIO_INTERRUPT_MESSAGE_INFO      MsiTable;
    BOOLEAN                         MsiConnected;

    /* DMA adapter for AllocateCommonBuffer / scatter-gather. */
    struct _DMA_ADAPTER*            DmaAdapter;
    ULONG                           NumberOfMapRegisters;

    /* A3: SG DMA description the driver passed via
     * NdisMRegisterScatterGatherDma. We need ProcessSGListHandler +
     * MaximumPhysicalMapping saved so NdisMAllocateNetBufferSGList
     * can call the driver back when the SGL is ready. */
    NDIS_SG_DMA_DESCRIPTION         SgDescription;
    BOOLEAN                         SgDescriptionValid;

    /* NDIS-owned, zero-normalized offload descriptors. */
    PVOID                           OffloadHwPtr;
    PVOID                           OffloadDefaultPtr;
    PVOID                           TcpOffloadHwPtr;
    PVOID                           TcpOffloadDefaultPtr;
    BOOLEAN                         OffloadValid;

    /* Phase 3 TX thunk: NBL pool used to wrap legacy NDIS_PACKETs when
     * forwarding sends from a legacy NDIS 5 protocol (tcpip.sys) into an
     * NDIS 6 miniport's SendNetBufferListsHandler. Bridge-owned wrapper
     * state lives in NdisReserved[1] so miniports keep MiniportReserved; NBL
     * allocation ownership is tracked in an allocator-private prefix. */
    NDIS_HANDLE                     TxWrapperNblPool;
    LIST_ENTRY                      InFlightNblsTx;
    KSPIN_LOCK                      TxLookupLock;

    /* A1: HaltEx send drain. TxInFlightCount tracks the number of wrapper
     * NBLs on InFlightNblsTx. TxDrainEvent is signaled each time the count
     * decrements; HaltEx waits on it with a timeout until the count reaches
     * zero, preventing MiniSendComplete from firing on a torn-down adapter. */
    LONG                            TxInFlightCount;
    KEVENT                          TxDrainEvent;
    BOOLEAN                         TxAccepting;

    /* A4: Pause/Restart state machine. A new NDIS 6 miniport is PAUSED
     * after MiniportInitializeEx and becomes RUNNING only when a protocol
     * opens/binds and RestartHandler succeeds. Drivers can return PENDING
     * from their PauseHandler / RestartHandler; we wait on the event. The
     * mutex serializes PnP, power, and binding transitions without holding a
     * spin lock across callbacks into the miniport. */
    KMUTEX                          PauseRestartMutex;
    KMUTEX                          StackTransitionMutex;
    ULONG                           PauseState;
    KEVENT                          PauseEvent;     /* pause complete */
    KEVENT                          RestartEvent;   /* restart complete */
    NDIS_STATUS                     PauseStatus;
    NDIS_STATUS                     RestartStatus;

    /* Keep one embedded work item so a low-memory condition cannot make us
     * forward a D-state transition without first notifying the miniport. The
     * busy state serializes that item through final IRP handoff and holds a
     * lifecycle rundown reference while it is queued. PowerPaused records
     * whether this transition owns the matching restart. */
    WORK_QUEUE_ITEM                 PowerWorkItem;
    PIRP                            PowerIrp;
    PDEVICE_OBJECT                  PowerLowerDevice;
    volatile LONG                   PowerWorkBusy;
    KEVENT                          PowerWorkIdleEvent;
    ULONG                           PowerWorkOperation;
    DEVICE_POWER_STATE              DevicePowerState;
    DEVICE_POWER_STATE              PowerTargetState;
    BOOLEAN                         PowerPaused;

    /* Phase 3 OID thunk: per-adapter waiter list. When a legacy NDIS 5
     * protocol calls NdisRequest with a set-OID (or an unknown query),
     * Ndis6LegacyDoRequest builds an NDIS_OID_REQUEST, registers a
     * NDIS6_OID_WAITER on this list BEFORE calling the miniport's
     * OidRequestHandler, then waits on the waiter's KEVENT.
     * NdisMOidRequestComplete pops the waiter via OidRequest->RequestId
     * and signals the event. */
    LIST_ENTRY                      OidWaiters;
    KSPIN_LOCK                      OidWaiterLock;

    /* Phase 3 RX thunk: legacy NDIS_PACKET / NDIS_BUFFER pools used to
     * wrap incoming NET_BUFFERs when indicating receives up to a legacy
     * NDIS 5 protocol (tcpip.sys). One legacy packet per NB; per-NBL
     * outstanding refcount lives in the NDIS-owned ChildRefCount field. */
    NDIS_HANDLE                     RxLegacyPacketPool;
    NDIS_HANDLE                     RxLegacyBufferPool;

    /* Per-adapter NDIS 6 filter chain. List head is the filter closest to
     * protocols and list tail is the filter closest to the miniport. */
    LIST_ENTRY                      FilterModuleList;
    KSPIN_LOCK                      FilterModuleListLock;

    /* Outstanding regular/direct filter OID traversals. The registry lets
     * NdisF*CancelOidRequest locate requests by their caller-owned RequestId
     * without repurposing that ABI field. */
    LIST_ENTRY                      FilterOidContextList;
    KSPIN_LOCK                      FilterOidContextListLock;

    /* Native NDIS 6 protocol bindings open on this adapter, linked via
     * NDIS6_PROTOCOL_BINDING.AdapterLink. The native datapath walks this
     * list to indicate receives and to validate an NBL's SourceHandle
     * before dereferencing it on send-complete; an empty list means only
     * the legacy NDIS 5 bridge datapath runs. */
    LIST_ENTRY                      ProtocolBindingList;
    KSPIN_LOCK                      ProtocolBindingListLock;
    volatile LONG                   ProtocolBindingsClosing;
} NDIS6_ADAPTER_EXT, *PNDIS6_ADAPTER_EXT;

/* ============================================================================
 *  NDIS 6 filter module — one per (filter driver × adapter) attachment.
 *  Created by Ndis6AttachFiltersToAdapter, freed by detach.
 * ============================================================================ */
struct _NDIS6_FILTER_DRIVER_BLOCK;
typedef struct _NDIS6_FILTER_MODULE
{
    ULONG                                   Signature;
    LIST_ENTRY                              ListEntry;
    LIST_ENTRY                              DriverLink;
    struct _NDIS6_FILTER_DRIVER_BLOCK*      DriverBlock;
    PLOGICAL_ADAPTER                        Adapter;
    NDIS_HANDLE                             FilterModuleContext;
    EX_RUNDOWN_REF                          RundownRef;
    volatile LONG                           Closing;
    volatile LONG                           State;
    KEVENT                                  PauseEvent;
    KEVENT                                  RestartEvent;
    NDIS_STATUS                             PauseStatus;
    NDIS_STATUS                             RestartStatus;
    NDIS_FILTER_PARTIAL_CHARACTERISTICS     PartialCharacteristics;
    BOOLEAN                                 PartialCharacteristicsValid;
    WORK_QUEUE_ITEM                         RestartWorkItem;
    volatile LONG                           RestartWorkQueued;
    BOOLEAN                                 SetAttributesCalled;
    /* D5: attributes the filter set via NdisFSetAttributes. The
     * NDIS_FILTER_ATTRIBUTES struct is just Header + Flags; we copy
     * them through so any future chain walk can honor Flags bits
     * like NDIS_FILTER_ATTRIBUTES_MANDATORY. */
    ULONG                                   Flags;
    BOOLEAN                                 AttributesValid;
} NDIS6_FILTER_MODULE, *PNDIS6_FILTER_MODULE;

#define NDIS6_FILTER_MODULE_SIGNATURE 'mFNn'
#define NDIS6_FILTER_STATE_PAUSED      0
#define NDIS6_FILTER_STATE_RESTARTING  1

#define NDIS6_ADD_DEVICE_NOT_CALLED 0
#define NDIS6_ADD_DEVICE_CALLING    1
#define NDIS6_ADD_DEVICE_SUCCEEDED  2
#define NDIS6_ADD_DEVICE_FAILED     3
#define NDIS6_ADD_DEVICE_REMOVING   4
#define NDIS6_ADD_DEVICE_REMOVED    5
#define NDIS6_FILTER_STATE_RUNNING     2
#define NDIS6_FILTER_STATE_PAUSING     3

static __inline BOOLEAN
Ndis6ReferenceFilterModule(
    PNDIS6_FILTER_MODULE Module)
{
    if (Module == NULL ||
        Module->Signature != NDIS6_FILTER_MODULE_SIGNATURE ||
        InterlockedCompareExchange(&Module->Closing, FALSE, FALSE) != FALSE)
    {
        return FALSE;
    }

    if (!ExAcquireRundownProtection(&Module->RundownRef))
        return FALSE;

    if (Module->Signature != NDIS6_FILTER_MODULE_SIGNATURE ||
        InterlockedCompareExchange(&Module->Closing, FALSE, FALSE) != FALSE)
    {
        ExReleaseRundownProtection(&Module->RundownRef);
        return FALSE;
    }

    return TRUE;
}

static __inline VOID
Ndis6DereferenceFilterModule(
    PNDIS6_FILTER_MODULE Module)
{
    ExReleaseRundownProtection(&Module->RundownRef);
}

/* ============================================================================
 *  Phase 3 OID waiter — used by Ndis6LegacyDoRequest to wait synchronously
 *  for the miniport's NdisMOidRequestComplete callback.
 * ============================================================================ */
typedef struct _NDIS6_OID_WAITER
{
    LIST_ENTRY          ListEntry;
    KEVENT              Event;
    PNDIS_OID_REQUEST   OidRequest;
    NDIS_STATUS         CompletionStatus;
} NDIS6_OID_WAITER, *PNDIS6_OID_WAITER;

#define NDIS6_EXT(adapter) ((PNDIS6_ADAPTER_EXT)((adapter)->Ndis6Context))
#define NDIS6_TAG          'rNRT'   /* "TRNn" — NDIS 6 bridge tag */

static __inline BOOLEAN
Ndis6ReferenceAdapterLifecycle(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6 ||
        (Ext = NDIS6_EXT(Adapter)) == NULL)
    {
        return FALSE;
    }

    return ExAcquireRundownProtection(&Ext->LifecycleRundown);
}

static __inline VOID
Ndis6DereferenceAdapterLifecycle(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);

    ASSERT(Ext != NULL);
    ExReleaseRundownProtection(&Ext->LifecycleRundown);
}

/* ============================================================================
 *  Bridge entry points
 * ============================================================================ */

/* 60driver.c */
VOID Ndis6DriverInit(VOID);

NDIS_STATUS
Ndis6RegisterMiniportDriverInternal(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS MiniportDriverCharacteristics,
    _In_opt_ PNDIS6_WDF_CX_DRIVER WdfCxDriver,
    _Out_ PNDIS_HANDLE NdisMiniportDriverHandle);

PNDIS6_DRIVER_BLOCK
Ndis6FindDriverBlock(
    _In_ PDRIVER_OBJECT DriverObject);

PLOGICAL_ADAPTER
Ndis6FindAdapterByFdo(
    _In_ PDEVICE_OBJECT DeviceObject);

VOID
Ndis6NotifyMiniportDevicePnPEvent(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ NDIS_DEVICE_PNP_EVENT Event);

NDIS_STATUS
Ndis6CallMiniportAddDevice(
    _In_ PLOGICAL_ADAPTER Adapter);

VOID
Ndis6CallMiniportRemoveDevice(
    _In_ PLOGICAL_ADAPTER Adapter);

NDIS_STATUS
Ndis6SetMiniportDevicePowerState(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ DEVICE_POWER_STATE DevicePowerState);

/* Returns KMDF's saved dispatch when DeviceObject is a KMDF-owned device
 * object on a hybrid driver, NULL when the IRP is ours to handle. */
PDRIVER_DISPATCH
Ndis6HybridGetOriginalDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR          MajorFunction);

/* User-mode IRP surface (create/close + IOCTL_NDIS_QUERY_GLOBAL_STATS) of
 * the miniport FDO. TRUE = DeviceObject is an adapter FDO and the IRP was
 * completed with *OutStatus. */
BOOLEAN
Ndis6TryDispatchAdapterFdoIrp(
    _In_  PDEVICE_OBJECT DeviceObject,
    _In_  PIRP           Irp,
    _Out_ NTSTATUS*      OutStatus);

/* 60stubs.c — TRUE if DeviceObject was created by NdisRegisterDeviceEx. */
BOOLEAN
Ndis6DeviceIsControlDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

/* 60adapter.c */
NDIS_STATUS
Ndis6CreateLogicalAdapter(
    _In_  PNDIS6_DRIVER_BLOCK   DriverBlock,
    _In_  PDEVICE_OBJECT        Pdo,
    _Out_ PLOGICAL_ADAPTER*     AdapterOut);

NDIS_STATUS
Ndis6CreateWdfLogicalAdapter(
    _In_ PNDIS6_DRIVER_BLOCK DriverBlock,
    _In_ PNDIS6_WDF_ADD_DEVICE_INFO AddDeviceInfo,
    _Out_ PLOGICAL_ADAPTER* AdapterOut);

VOID
Ndis6DestroyLogicalAdapter(
    _In_ PLOGICAL_ADAPTER       Adapter);

NDIS_STATUS
Ndis6CallMiniportInitializeEx(
    _In_ PLOGICAL_ADAPTER       Adapter);

VOID
Ndis6CallMiniportHaltEx(
    _In_ PLOGICAL_ADAPTER       Adapter,
    _In_ NDIS_HALT_ACTION       HaltAction);

/* Intermediate-driver upper-miniport instantiation (NdisIMInitializeDeviceInstanceEx).
 * Creates a virtual 802.3 miniport with no PnP PDO, runs the driver's
 * InitializeHandlerEx, and binds protocols. */
NDIS_STATUS
Ndis6InitializeImDeviceInstance(
    _In_ PNDIS6_DRIVER_BLOCK    DriverBlock,
    _In_ PCUNICODE_STRING       DeviceName,
    _In_ NDIS_HANDLE            DeviceContext);

/* A4: Pause/Restart state-machine helpers. Called around filter
 * attach/detach, HaltEx, and power transitions. Both wait on the
 * respective event if the driver returned PENDING. */
NDIS_STATUS
Ndis6CallMiniportPauseEx(
    _In_ PLOGICAL_ADAPTER       Adapter);

NDIS_STATUS
Ndis6CallMiniportRestartEx(
    _In_ PLOGICAL_ADAPTER       Adapter);

NDIS_STATUS
Ndis6PauseDriverStack(
    _In_ PLOGICAL_ADAPTER       Adapter);

NDIS_STATUS
Ndis6RestartDriverStack(
    _In_ PLOGICAL_ADAPTER       Adapter);

/* Call only while holding Adapter->StackTransitionMutex. */
NDIS_STATUS
Ndis6PauseDriverStackLocked(
    _In_ PLOGICAL_ADAPTER       Adapter);

NDIS_STATUS
Ndis6RestartDriverStackLocked(
    _In_ PLOGICAL_ADAPTER       Adapter);

/* PauseState constants — stored in NDIS6_ADAPTER_EXT.PauseState. */
#define NDIS6_PAUSE_STATE_RUNNING     0
#define NDIS6_PAUSE_STATE_PAUSING     1
#define NDIS6_PAUSE_STATE_PAUSED      2
#define NDIS6_PAUSE_STATE_RESTARTING  3

#define NDIS6_INTERRUPT_RUNDOWN_STOPPING ((LONG)0x80000000)

/* 60io.c */
NDIS_STATUS
Ndis6IoInitDmaAdapter(
    _In_ PNDIS6_ADAPTER_EXT     Ext,
    _In_ PDEVICE_OBJECT         Pdo);

VOID
Ndis6IoFreeDmaAdapter(
    _In_ PNDIS6_ADAPTER_EXT     Ext);

VOID
Ndis6DisconnectInterrupt(
    _In_ PNDIS6_ADAPTER_EXT     Ext);

/* 60oid.c — legacy NDIS_REQUEST dispatcher for NDIS 6 adapters.
 * Called from MiniDoRequest (miniport.c) when Adapter->IsNdis6 is TRUE,
 * before the code would otherwise dereference the NULL DriverHandle
 * that NDIS 5 miniports keep their MiniportCharacteristics inside. */
NDIS_STATUS
Ndis6LegacyDoRequest(
    _In_ PLOGICAL_ADAPTER       Adapter,
    _In_ PNDIS_REQUEST          Request);

/* 60filter.c — datapath chain walk for the NDIS 6 filter framework.
 * Filters attach to an adapter via NdisFRegisterFilterDriver +
 * AttachHandler; on TX/RX/return, the bridge walks the per-adapter
 * filter chain calling each filter's Send/Recv/Return handler in
 * order. The filter calls back into NdisF* helpers which pop the
 * "current position" from the chain and forward to the next filter
 * (or to the miniport at the bottom). */
VOID
Ndis6FilterDispatchSend(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             SendFlags);

VOID
Ndis6FilterDispatchSendComplete(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             SendCompleteFlags);

VOID
Ndis6FilterDispatchReceive(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferLists,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             NumberOfNetBufferLists,
    _In_ ULONG             ReceiveFlags);

VOID
Ndis6FilterDispatchReturn(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             ReturnFlags);

VOID
Ndis6FilterDispatchCancelSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PVOID CancelId);

VOID
Ndis6FilterDispatchStatus(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_STATUS_INDICATION StatusIndication);

VOID
Ndis6FilterDispatchDevicePnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent);

NDIS_STATUS
Ndis6FilterDispatchNetPnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification);

/* The bridge's terminal handlers — what runs when a filter chain walk
 * reaches the end (TX → miniport, RX → indicate to legacy protocols). */
VOID
Ndis6FilterTerminalSend(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             SendFlags);

VOID
Ndis6FilterTerminalReceive(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferLists,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             NumberOfNetBufferLists,
    _In_ ULONG             ReceiveFlags);

VOID
Ndis6FilterTerminalReturn(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             ReturnFlags);

VOID
Ndis6FilterTerminalCancelSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PVOID CancelId);

VOID
Ndis6FilterTerminalStatus(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_STATUS_INDICATION StatusIndication);

VOID
Ndis6FilterTerminalDevicePnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent);

NDIS_STATUS
Ndis6FilterTerminalNetPnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification);

VOID
Ndis6FilterTerminalSendComplete(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             SendCompleteFlags);

/* NdisF* helper APIs — declared here because the public ddk/ndis.h
 * doesn't have them. Filters call these to push NBLs through the
 * chain; the bridge resolves "next filter" via the per-adapter list. */
VOID NTAPI
NdisFSendNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             SendFlags);

VOID NTAPI
NdisFSendNetBufferListsComplete(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             SendCompleteFlags);

VOID NTAPI
NdisFIndicateReceiveNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             NumberOfNetBufferLists,
    _In_ ULONG             ReceiveFlags);

VOID NTAPI
NdisFReturnNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             ReturnFlags);

NDIS_STATUS NTAPI
NdisFSetAttributes(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ NDIS_HANDLE       FilterModuleContext,
    _In_ PNDIS_FILTER_ATTRIBUTES FilterAttributes);

NDIS_STATUS NTAPI
NdisFOidRequest(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest);

VOID NTAPI
NdisFOidRequestComplete(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status);

/* Filter chain dispatch entry for OIDs — called from Ndis6OidForward
 * (60oid.c) so OID requests pass through any installed filters before
 * reaching the miniport. */
NDIS_STATUS
Ndis6FilterDispatchOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest);

BOOLEAN
Ndis6FilterCompleteOidFromMiniport(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status);

NDIS_STATUS
Ndis6FilterDispatchDirectOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest);

NDIS_STATUS
Ndis6FilterDispatchSynchronousOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest);

BOOLEAN
Ndis6FilterCompleteDirectOidFromMiniport(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status);

VOID
Ndis6FilterCancelOidRequestFromProtocol(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ NDIS_HANDLE        OriginHandle,
    _In_ PVOID             RequestId,
    _In_ BOOLEAN           DirectRequest);

VOID
Ndis6CompleteOidRequestToOrigin(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status);

VOID
Ndis6CompleteDirectOidRequestToOrigin(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status);

NDIS_STATUS
Ndis6FilterTerminalOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest);

NDIS_STATUS
Ndis6FilterTerminalDirectOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest);

/* ============================================================================
 *  60util.c — NDIS 6 utility APIs
 *
 *  Timer objects, RW locks, NetBuffer helpers, NdisGetVersion overrides
 *  for the NT 6.1 target. These are general-purpose primitives every
 *  third-party NDIS 6 driver expects from ndis.sys.
 * ============================================================================ */

/* NDIS 6 RW lock — wraps an EX_PUSH_LOCK or simple shared/exclusive
 * primitive. Drivers use NdisAllocateRWLock + NdisAcquire/Release/Free. */
typedef struct _NDIS_RW_LOCK_EX NDIS_RW_LOCK_EX, *PNDIS_RW_LOCK_EX;

typedef struct _LOCK_STATE_EX
{
    PVOID  Reserved[2];
} LOCK_STATE_EX, *PLOCK_STATE_EX;

PNDIS_RW_LOCK_EX NTAPI
NdisAllocateRWLock(
    _In_opt_ NDIS_HANDLE NdisHandle);

VOID NTAPI
NdisFreeRWLock(
    _In_ PNDIS_RW_LOCK_EX Lock);

VOID NTAPI
NdisAcquireRWLockRead(
    _In_  PNDIS_RW_LOCK_EX Lock,
    _Out_ PLOCK_STATE_EX   LockState,
    _In_  UCHAR            Flags);

VOID NTAPI
NdisAcquireRWLockWrite(
    _In_  PNDIS_RW_LOCK_EX Lock,
    _Out_ PLOCK_STATE_EX   LockState,
    _In_  UCHAR            Flags);

VOID NTAPI
NdisReleaseRWLock(
    _In_ PNDIS_RW_LOCK_EX Lock,
    _In_ PLOCK_STATE_EX   LockState);

/* ============================================================================
 *  NDIS 6 protocol open/close adapter
 *
 *  A native NDIS 6 protocol calls NdisOpenAdapterEx from inside its
 *  ProtocolBindAdapterEx callback (or later) to formally take a binding
 *  on an adapter. The bridge allocates an NDIS6_PROTOCOL_BINDING and
 *  hands the address back as the binding handle. NdisCloseAdapterEx
 *  reverses the operation.
 * ============================================================================ */

NDIS_STATUS NTAPI
NdisOpenAdapterEx(
    _In_  NDIS_HANDLE  NdisProtocolHandle,
    _In_  NDIS_HANDLE  ProtocolBindingContext,
    _In_  PNDIS_OPEN_PARAMETERS OpenParameters,
    _In_  NDIS_HANDLE  BindContext,
    _Out_ PNDIS_HANDLE NdisBindingHandle);

NDIS_STATUS NTAPI
NdisCloseAdapterEx(
    _In_ NDIS_HANDLE NdisBindingHandle);

/* ============================================================================
 *  NDIS 6 filter driver block — one per NdisFRegisterFilterDriver caller.
 * ============================================================================ */
typedef struct _NDIS6_FILTER_DRIVER_BLOCK
{
    LIST_ENTRY                          ListEntry;
    LIST_ENTRY                          ModuleList;
    KSPIN_LOCK                          ModuleListLock;
    EX_RUNDOWN_REF                      CallbackRundown;
    ULONG                               Signature;
    volatile LONG                       Closing;
    PDRIVER_OBJECT                      DriverObject;
    NDIS_HANDLE                         FilterDriverContext;
    PWCHAR                              FriendlyNameBuffer;
    PWCHAR                              UniqueNameBuffer;
    PWCHAR                              ServiceNameBuffer;
    NDIS_FILTER_DRIVER_CHARACTERISTICS  Characteristics;
} NDIS6_FILTER_DRIVER_BLOCK, *PNDIS6_FILTER_DRIVER_BLOCK;

#define NDIS6_FILTER_DRIVER_SIGNATURE 'dFNn'

extern LIST_ENTRY g_Ndis6FilterDriverList;
extern KSPIN_LOCK g_Ndis6FilterDriverListLock;

NDIS_STATUS
Ndis6AttachFiltersToAdapter(
    _In_ PLOGICAL_ADAPTER Adapter);

VOID
Ndis6DetachFiltersFromAdapter(
    _In_ PLOGICAL_ADAPTER Adapter);

NDIS_STATUS
Ndis6PauseFilterModules(
    _In_ PLOGICAL_ADAPTER Adapter);

NDIS_STATUS
Ndis6RestartFilterModules(
    _In_ PLOGICAL_ADAPTER Adapter);

/* ============================================================================
 *  NDIS 6 protocol driver block — one per NdisRegisterProtocolDriver caller.
 * ============================================================================ */
typedef struct _NDIS6_PROTOCOL_DRIVER_BLOCK
{
    ULONG                                   Signature;
    LIST_ENTRY                              ListEntry;
    LIST_ENTRY                              BindingList;
    KSPIN_LOCK                              BindingListLock;
    EX_RUNDOWN_REF                          CallbackRundown;
    volatile LONG                           Closing;
    PWCHAR                                  NameBuffer;
    NDIS_HANDLE                             ProtocolDriverContext;
    NDIS_PROTOCOL_DRIVER_CHARACTERISTICS    Characteristics;
} NDIS6_PROTOCOL_DRIVER_BLOCK, *PNDIS6_PROTOCOL_DRIVER_BLOCK;

#define NDIS6_PROTOCOL_DRIVER_SIGNATURE 'dPNn'

/* ============================================================================
 *  NDIS 6 protocol binding — one per (protocol driver × adapter) open.
 *  Created by NdisOpenAdapterEx, freed by NdisCloseAdapterEx. The driver
 *  receives the binding handle (which IS this struct cast to NDIS_HANDLE)
 *  and uses it for all subsequent operations on that binding (NdisOidRequest,
 *  NdisSendNetBufferLists, etc.).
 * ============================================================================ */
typedef struct _NDIS6_PROTOCOL_BINDING
{
    ULONG                                   Signature;
    /* Links every open binding owned by DriverBlock. */
    LIST_ENTRY                              ListEntry;
    PNDIS6_PROTOCOL_DRIVER_BLOCK            DriverBlock;
    PLOGICAL_ADAPTER                        Adapter;
    NDIS_HANDLE                             ProtocolBindingContext;
    /* Links this binding into the owning adapter's Ext->ProtocolBindingList
     * (inserted by NdisOpenAdapterEx, removed by NdisCloseAdapterEx). */
    LIST_ENTRY                              AdapterLink;
    /* D4: list of in-flight async OID requests issued by this protocol
     * binding. Each pending NdisOidRequest adds an entry; the matching
     * NdisMOidRequestComplete walks it to find the binding to notify. */
    LIST_ENTRY                              PendingOidRequests;
    KSPIN_LOCK                              PendingOidRequestsLock;
    EX_RUNDOWN_REF                          RundownRef;
    volatile LONG                           Closing;
    volatile LONG                           State;
} NDIS6_PROTOCOL_BINDING, *PNDIS6_PROTOCOL_BINDING;

#define NDIS6_PROTOCOL_BINDING_SIGNATURE 'bPNn'

#define NDIS6_PROTOCOL_STATE_PAUSED     0
#define NDIS6_PROTOCOL_STATE_RESTARTING 1
#define NDIS6_PROTOCOL_STATE_RUNNING    2
#define NDIS6_PROTOCOL_STATE_PAUSING    3

static __inline BOOLEAN
Ndis6ReferenceProtocolBinding(
    PNDIS6_PROTOCOL_BINDING Binding)
{
    if (Binding == NULL ||
        Binding->Signature != NDIS6_PROTOCOL_BINDING_SIGNATURE ||
        InterlockedCompareExchange(&Binding->Closing, FALSE, FALSE) != FALSE)
        return FALSE;

    if (!ExAcquireRundownProtection(&Binding->RundownRef))
        return FALSE;

    if (Binding->Signature != NDIS6_PROTOCOL_BINDING_SIGNATURE ||
        InterlockedCompareExchange(&Binding->Closing, FALSE, FALSE) != FALSE)
    {
        ExReleaseRundownProtection(&Binding->RundownRef);
        return FALSE;
    }

    return TRUE;
}

static __inline VOID
Ndis6DereferenceProtocolBinding(
    PNDIS6_PROTOCOL_BINDING Binding)
{
    ExReleaseRundownProtection(&Binding->RundownRef);
}

BOOLEAN
Ndis6RxReturnNativeNetBufferList(
    _In_ PNDIS6_PROTOCOL_BINDING Binding,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG ReturnFlags);

/* D4: per-async-OID context. Stashed in the NDIS-owned request slots by
 * NdisOidRequest so NdisMOidRequestComplete can find the binding and call
 * the protocol's OidRequestCompleteHandler without changing RequestId. */
typedef struct _NDIS6_PROTOCOL_PENDING_OID
{
    ULONG                                   Signature;
    LIST_ENTRY                              ListEntry;
    struct _NDIS6_PROTOCOL_BINDING*         Binding;
    volatile LONG                           References;
    BOOLEAN                                 Listed;
    BOOLEAN                                 DirectRequest;
} NDIS6_PROTOCOL_PENDING_OID, *PNDIS6_PROTOCOL_PENDING_OID;

#define NDIS6_PROTOCOL_PENDING_OID_SIGNATURE 'dOPn'
#define NDIS6_PROTOCOL_PENDING_OID_TAG       'dOPn'

extern const UCHAR Ndis6ProtocolOidContextMarker;

static __inline PNDIS6_PROTOCOL_PENDING_OID
Ndis6GetPendingOidContext(
    PNDIS_OID_REQUEST OidRequest)
{
    PVOID Pending;
    PVOID Marker;

    RtlCopyMemory(&Pending, &OidRequest->NdisReserved[2 * sizeof(PVOID)], sizeof(Pending));
    RtlCopyMemory(&Marker, &OidRequest->NdisReserved[3 * sizeof(PVOID)], sizeof(Marker));
    if (Marker != (PVOID)&Ndis6ProtocolOidContextMarker)
        return NULL;
    return (PNDIS6_PROTOCOL_PENDING_OID)Pending;
}

static __inline VOID
Ndis6SetPendingOidContext(
    PNDIS_OID_REQUEST OidRequest,
    PNDIS6_PROTOCOL_PENDING_OID Pending)
{
    PVOID Marker = (PVOID)&Ndis6ProtocolOidContextMarker;

    RtlCopyMemory(&OidRequest->NdisReserved[2 * sizeof(PVOID)], &Pending, sizeof(Pending));
    RtlCopyMemory(&OidRequest->NdisReserved[3 * sizeof(PVOID)], &Marker, sizeof(Marker));
}

static __inline VOID
Ndis6ClearPendingOidContext(
    PNDIS_OID_REQUEST OidRequest)
{
    PVOID Empty = NULL;

    RtlCopyMemory(&OidRequest->NdisReserved[2 * sizeof(PVOID)], &Empty, sizeof(Empty));
    RtlCopyMemory(&OidRequest->NdisReserved[3 * sizeof(PVOID)], &Empty, sizeof(Empty));
}

static __inline VOID
Ndis6ReferencePendingOid(
    PNDIS6_PROTOCOL_PENDING_OID Pending)
{
    InterlockedIncrement(&Pending->References);
}

static __inline VOID
Ndis6DereferencePendingOid(
    PNDIS6_PROTOCOL_PENDING_OID Pending)
{
    if (InterlockedDecrement(&Pending->References) == 0)
    {
        PNDIS6_PROTOCOL_BINDING Binding = Pending->Binding;

        Pending->Signature = 0;
        ExFreePoolWithTag(Pending, NDIS6_PROTOCOL_PENDING_OID_TAG);
        Ndis6DereferenceProtocolBinding(Binding);
    }
}

BOOLEAN
Ndis6ReferenceNativeTransmit(
    _In_ PNDIS6_ADAPTER_EXT Ext);

VOID
Ndis6DereferenceTransmit(
    _In_ PNDIS6_ADAPTER_EXT Ext);

extern LIST_ENTRY g_Ndis6ProtocolDriverList;
extern KSPIN_LOCK g_Ndis6ProtocolDriverListLock;

VOID
Ndis6UnbindAllProtocolsFromAdapter(
    _In_ PLOGICAL_ADAPTER Adapter);

NDIS_STATUS
Ndis6PauseProtocolBindings(
    _In_ PLOGICAL_ADAPTER Adapter);

VOID
Ndis6RestartProtocolBindings(
    _In_ PLOGICAL_ADAPTER Adapter);

NDIS_STATUS
Ndis6NotifyProtocolBindingsPower(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ DEVICE_POWER_STATE DevicePowerState);

/* NDIS 6 helpers exposed to the legacy library so the public NdisM*
 * exports in ndis/io.c and ndis/memory.c can dispatch on IsNdis6
 * without duplicating symbols. The legacy versions add a one-line
 * IsNdis6 check at the top and forward to these. */
NDIS_STATUS NTAPI
Ndis6MMapIoSpace(
    _Out_ PVOID* VirtualAddress,
    _In_  NDIS_HANDLE MiniportAdapterHandle,
    _In_  NDIS_PHYSICAL_ADDRESS PhysicalAddress,
    _In_  UINT Length);

VOID NTAPI
Ndis6MUnmapIoSpace(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ PVOID VirtualAddress,
    _In_ UINT Length);

NDIS_STATUS NTAPI
Ndis6MRegisterIoPortRange(
    _Out_ PVOID* PortOffset,
    _In_  NDIS_HANDLE MiniportAdapterHandle,
    _In_  UINT InitialPort,
    _In_  UINT NumberOfPorts);

VOID NTAPI
Ndis6MDeregisterIoPortRange(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ UINT InitialPort,
    _In_ UINT NumberOfPorts,
    _In_ PVOID PortOffset);

VOID NTAPI
Ndis6MAllocateSharedMemory(
    _In_  NDIS_HANDLE MiniportAdapterHandle,
    _In_  ULONG Length,
    _In_  BOOLEAN Cached,
    _Out_ PVOID* VirtualAddress,
    _Out_ PNDIS_PHYSICAL_ADDRESS PhysicalAddress);

VOID NTAPI
Ndis6MFreeSharedMemory(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ ULONG Length,
    _In_ BOOLEAN Cached,
    _In_ PVOID VirtualAddress,
    _In_ NDIS_PHYSICAL_ADDRESS PhysicalAddress);

#endif /* _NDIS6_INTERNAL_H_ */
