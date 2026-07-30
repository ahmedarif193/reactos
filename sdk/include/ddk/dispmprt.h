/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     WDDM display miniport driver public interface header
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * This header is the primary public interface for WDDM display miniport
 * drivers (dxgkrnl clients).  It corresponds to the Vista WDK dispmprt.h
 * and must be included by every display miniport driver.
 *
 * Include chain:
 *   dispmprt.h
 *     d3dkmddi.h
 *       d3dkmdt.h
 *         d3dukmdt.h
 *
 * Usage:
 *   #define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_VISTA
 *   #include <dispmprt.h>
 *
 * Notes on naming:
 *   The #define macros DXGK_INTERRUPT_DMA_COMPLETED etc. use integer
 *   constants that correspond to the DXGK_INTERRUPT_TYPE enum values
 *   defined in d3dkmddi.h.  Callers may use either form.
 */

#ifndef _DISPMPRT_H_
#define _DISPMPRT_H_

#pragma warning(push)
#pragma warning(disable:4200) /* zero-length arrays in structs          */
#pragma warning(disable:4201) /* nameless struct/union                  */

/*
 * d3dukmdt.h (pulled in transitively via d3dkmddi.h → d3dkmdt.h → d3dukmdt.h)
 * uses UINT before any kernel header defines that type.  windef.h supplies it
 * via minwindef.h.  This is safe in kernel mode — other ReactOS kernel drivers
 * use the same pattern (see dxgkrnl_private.h).
 */
#include <windef.h>
#include <guiddef.h>

/*
 * d3dukmdt.h defaults an unspecified DXGKDDI_INTERFACE_VERSION to the newest
 * known WDDM level.  dispmprt.h is the miniport entrypoint, so preserve the
 * Vista-era default before d3dkmddi.h pulls d3dukmdt.h in.
 */
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x1052 /* DXGKDDI_INTERFACE_VERSION_VISTA */
#endif

/*
 * d3dkmddi.h defines all DXGKARG_* argument structures and the
 * DXGK_INTERRUPT_TYPE enum.  It transitively includes d3dkmdt.h and
 * d3dukmdt.h which supply D3DDDI_* types.
 */
#include <d3dkmddi.h>
#include <acpiioct.h>

#ifndef _IRQL_requires_DXGK_
#define _IRQL_requires_DXGK_(level) _IRQL_requires_(level)
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
DEFINE_GUID(GUID_WDDM_INTERFACE_DISPLAYMUX,
    0x086467fb, 0xdddf, 0x4c19, 0x97, 0xd5, 0xc4, 0x1d, 0x76, 0x72, 0x21, 0xc7);
DEFINE_GUID(GUID_WDDM_INTERFACE_DISPLAYMUX_2,
    0x086467fb, 0xdddf, 0x4c19, 0x97, 0xd5, 0xc4, 0x1d, 0x76, 0x72, 0x21, 0xc8);
DEFINE_GUID(GUID_WDDM_INTERFACE_FEATURE,
    0x94bb3993, 0xc6c3, 0x4da7, 0x89, 0x49, 0xa1, 0x13, 0x82, 0x32, 0xe7, 0x59);
DEFINE_GUID(GUID_WDDM_INTERFACE_WAITWAKE,
    0xd3a8ec81, 0xbdef, 0x43d6, 0x94, 0x71, 0x22, 0x38, 0x14, 0x60, 0x5e, 0x38);
#endif

/* =========================================================================
 * Forward declarations for types defined in other DDK headers
 *
 * dispmprt.h is self-contained and does not require the caller to include
 * video.h.  The types below are opaque to dispmprt.h; full definitions are
 * in video.h (QUERY_INTERFACE, VIDEO_REQUEST_PACKET) and are only required
 * if the miniport implements the corresponding callbacks.
 * =========================================================================
 */

/* video.h: VRP passed to DxgkDdiDispatchIoRequest */
typedef struct _VIDEO_REQUEST_PACKET   VIDEO_REQUEST_PACKET;
typedef struct _VIDEO_REQUEST_PACKET  *PVIDEO_REQUEST_PACKET;

/* video.h: passed to DxgkDdiQueryInterface */
typedef struct _QUERY_INTERFACE
{
    CONST GUID *InterfaceType;
    USHORT      Size;
    USHORT      Version;
    PINTERFACE  Interface;
    PVOID       InterfaceSpecificData;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    ULONG       DeviceUid;
#endif
} QUERY_INTERFACE, *PQUERY_INTERFACE;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
#ifdef _WIN64
C_ASSERT(sizeof(QUERY_INTERFACE) == 0x28);
C_ASSERT(FIELD_OFFSET(QUERY_INTERFACE, DeviceUid) == 0x20);
#else
C_ASSERT(sizeof(QUERY_INTERFACE) == 0x14);
C_ASSERT(FIELD_OFFSET(QUERY_INTERFACE, DeviceUid) == 0x10);
#endif
#else
#ifdef _WIN64
C_ASSERT(sizeof(QUERY_INTERFACE) == 0x20);
#else
C_ASSERT(sizeof(QUERY_INTERFACE) == 0x10);
#endif
#endif

/* Multi-GPU linked adapter descriptor (no public header yet) */
typedef struct _LINKED_DEVICE          LINKED_DEVICE;
typedef struct _LINKED_DEVICE         *PLINKED_DEVICE;

typedef _In_ CONST PDEVICE_OBJECT      IN_CONST_PDEVICE_OBJECT;

/* =========================================================================
 * Interface version constants
 *
 * Defines which WDDM revision is being targeted.  The miniport must set
 * DRIVER_INITIALIZATION_DATA.Version to one of these values.
 * =========================================================================
 */

/*
 * d3dkmddi.h has already included d3dukmdt.h, which is the single canonical
 * source for these selectors. Keep this check here because Version controls
 * how many bytes dxgkrnl may read from DRIVER_INITIALIZATION_DATA; a stale
 * duplicate value changes the callback-table ABI without changing its type.
 */
#if !defined(DXGKDDI_INTERFACE_VERSION_VISTA) || \
    !defined(DXGKDDI_INTERFACE_VERSION_WDDM3_2)
#error d3dukmdt.h did not provide the WDDM interface selectors
#endif

#if (DXGKDDI_INTERFACE_VERSION_VISTA != 0x1052) || \
    (DXGKDDI_INTERFACE_VERSION_VISTA_SP1 != 0x1053) || \
    (DXGKDDI_INTERFACE_VERSION_WIN7 != 0x2005) || \
    (DXGKDDI_INTERFACE_VERSION_WIN8 != 0x300E) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM1_3 != 0x4002) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_0 != 0x5023) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_1 != 0x6003) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_1_5 != 0x6010) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_1_6 != 0x6011) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_2 != 0x700A) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_3 != 0x8001) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_4 != 0x9006) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_5 != 0xA00B) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_6 != 0xB004) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_7 != 0xC004) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_8 != 0xD001) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM2_9 != 0xE003) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM3_0 != 0xF003) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM3_1 != 0x10004) || \
    (DXGKDDI_INTERFACE_VERSION_WDDM3_2 != 0x11007)
#error d3dukmdt.h contains an unsupported WDDM interface selector set
#endif

/* =========================================================================
 * GPU interrupt type constants
 *
 * These #define macros are integer values that match the corresponding
 * DXGK_INTERRUPT_TYPE enum members in d3dkmddi.h.  Legacy code and
 * dxgkrnl's interrupt dispatch path use the integer form.
 * =========================================================================
 */
#define DXGK_INTERRUPT_DMA_COMPLETED                        1
#define DXGK_INTERRUPT_DMA_PREEMPTED                        2
#define DXGK_INTERRUPT_CRTC_VSYNC                           3
#define DXGK_INTERRUPT_DMA_FAULTED                          4
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
#define DXGK_INTERRUPT_DISPLAYONLY_VSYNC                    5
#define DXGK_INTERRUPT_DISPLAYONLY_PRESENT_PROGRESS         6
#define DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY   7
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
#define DXGK_INTERRUPT_MICACAST_CHUNK_PROCESSING_COMPLETE   8
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
#define DXGK_INTERRUPT_DMA_PAGE_FAULTED                     9
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
#define DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2 10
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
#define DXGK_INTERRUPT_MONITORED_FENCE_SIGNALED             11
#define DXGK_INTERRUPT_HWQUEUE_PAGE_FAULTED                 12
#define DXGK_INTERRUPT_HWCONTEXTLIST_SWITCH_COMPLETED       13
#define DXGK_INTERRUPT_PERIODIC_MONITORED_FENCE_SIGNALED    14
#define DXGK_INTERRUPT_PERIODICED_MONITORED_FENCE_SIGNALED  DXGK_INTERRUPT_PERIODIC_MONITORED_FENCE_SIGNALED
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
#define DXGK_INTERRUPT_SCHEDULING_LOG_INTERRUPT             15
#define DXGK_INTERRUPT_GPU_ENGINE_TIMEOUT                   16
#define DXGK_INTERRUPT_SUSPEND_CONTEXT_COMPLETED            17
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
#define DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY3  18
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
#define DXGK_INTERRUPT_NATIVE_FENCE_SIGNALED                19
#define DXGK_INTERRUPT_GPU_ENGINE_STATE_CHANGE              20
#endif


/* =========================================================================
 * DXGK_CHILD_DEVICE_TYPE
 *
 * Identifies the functional category of a child device reported by
 * DxgkDdiQueryChildRelations.
 * =========================================================================
 */
typedef enum _DXGK_CHILD_DEVICE_TYPE
{
    TypeUninitialized   = 0,
    TypeVideoOutput     = 1,
    TypeOther           = 2,
    TypeIntegratedDisplay = 3,
} DXGK_CHILD_DEVICE_TYPE;


/*
 * DXGK_CHILD_DEVICE_HPD_AWARENESS is defined in d3dkmdt.h which is
 * transitively included via d3dkmddi.h.  It is not redefined here.
 */


/* =========================================================================
 * DXGK_CHILD_CAPABILITIES
 *
 * Capability flags for a child device, embedded in DXGK_CHILD_DESCRIPTOR.
 * =========================================================================
 */
/*
 * DXGK_VIDEO_OUTPUT_CAPABILITIES
 *
 * Per-child-output capability bits, embedded in DXGK_CHILD_CAPABILITIES.
 */
typedef struct _DXGK_VIDEO_OUTPUT_CAPABILITIES
{
    D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY     InterfaceTechnology;
    D3DKMDT_MONITOR_ORIENTATION_AWARENESS MonitorOrientationAwareness;
    BOOLEAN                             SupportsSdtvModes;
} DXGK_VIDEO_OUTPUT_CAPABILITIES;

typedef struct _DXGK_INTEGRATED_DISPLAY_CHILD
{
    D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY InterfaceTechnology;
    USHORT                          DescriptorLength;
} DXGK_INTEGRATED_DISPLAY_CHILD, *PDXGK_INTEGRATED_DISPLAY_CHILD;

typedef struct _DXGK_CHILD_CAPABILITIES
{
    union
    {
        DXGK_VIDEO_OUTPUT_CAPABILITIES VideoOutput;
        struct
        {
            UINT MustBeZero;
        } Other;
        DXGK_INTEGRATED_DISPLAY_CHILD IntegratedDisplayChild;
    } Type;
    DXGK_CHILD_DEVICE_HPD_AWARENESS HpdAwareness;
} DXGK_CHILD_CAPABILITIES;


/* =========================================================================
 * DXGK_CHILD_DESCRIPTOR
 *
 * Describes one child device.  The miniport fills an array of these in
 * DxgkDdiQueryChildRelations.
 * =========================================================================
 */
typedef struct _DXGK_CHILD_DESCRIPTOR
{
    DXGK_CHILD_DEVICE_TYPE      ChildDeviceType;
    DXGK_CHILD_CAPABILITIES     ChildCapabilities;
    ULONG                       AcpiUid;
    ULONG                       ChildUid;
} DXGK_CHILD_DESCRIPTOR, *PDXGK_CHILD_DESCRIPTOR;


/* =========================================================================
 * DXGK_CHILD_STATUS_TYPE / DXGK_CHILD_STATUS
 *
 * Passed to DxgkCbIndicateChildStatus to report a hot-plug or rotation event.
 * =========================================================================
 */
typedef enum _DXGK_CHILD_STATUS_TYPE
{
    StatusUninitialized = 0,
    StatusConnection    = 1,
    StatusRotation      = 2,
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    StatusMiracastConnection = 3,
#endif
} DXGK_CHILD_STATUS_TYPE;

typedef struct _DXGK_CHILD_STATUS
{
    DXGK_CHILD_STATUS_TYPE  Type;
    ULONG                   ChildUid;
    union
    {
        struct { BOOLEAN Connected; } HotPlug;
        struct { UCHAR   Angle;     } Rotation;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
        struct
        {
            BOOLEAN Connected;
            D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY MiracastMonitorType;
        } Miracast;
#endif
    };
} DXGK_CHILD_STATUS, *PDXGK_CHILD_STATUS;

typedef
    _Function_class_DXGK_(DXGKDDI_PROTECTED_CALLBACK)
    _IRQL_requires_(PASSIVE_LEVEL)
VOID
(*DXGKDDI_PROTECTED_CALLBACK)(
    IN_CONST_PVOID MiniportDeviceContext,
    _In_ PVOID ProtectedCallbackContext,
    _In_ NTSTATUS ProtectionStatus);


/* =========================================================================
 * DXGK_DEVICE_DESCRIPTOR
 *
 * Carries an EDID or other descriptor blob queried via
 * DxgkDdiQueryDeviceDescriptor.
 * =========================================================================
 */
typedef struct _DXGK_DEVICE_DESCRIPTOR
{
    ULONG   DescriptorOffset;
    ULONG   DescriptorLength;
    PVOID   DescriptorBuffer;
} DXGK_DEVICE_DESCRIPTOR, *PDXGK_DEVICE_DESCRIPTOR;


/* =========================================================================
 * DXGK_DEVICE_INFO
 *
 * Hardware resource information filled by dxgkrnl and handed to the
 * miniport's DxgkDdiStartDevice callback.
 *
 * This layout matches the Vista WDK definition exactly.
 * =========================================================================
 */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)

#define DXGK_DISPLAYMUX_INTERFACE_VERSION_1 0x01

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_GET_DRIVER_SUPPORT_LEVEL)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_GET_DRIVER_SUPPORT_LEVEL)(
    _In_ PVOID DriverContext,
    _Out_ PDXGK_DISPLAYMUX_SUPPORT_LEVEL pDriverSupportLevel);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_GET_RUNTIME_STATUS)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_GET_RUNTIME_STATUS)(
    _In_ PVOID DriverContext,
    _Out_ PDXGK_DISPLAYMUX_RUNTIME_STATUS pRuntimeStatus);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_PRE_SWITCH_AWAY)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_PRE_SWITCH_AWAY)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId,
    _Out_ PULONG pSwitchPrivateDataSize);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_PRE_SWITCH_AWAY_GET_PRIVATE_DATA)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_PRE_SWITCH_AWAY_GET_PRIVATE_DATA)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG SwitchPrivateDataSize,
    _Out_writes_bytes_(SwitchPrivateDataSize) PVOID pSwitchPrivateDataBuffer,
    _Out_ GUID *pSwitchPrivateDataGUID);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_PRE_SWITCH_TO)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_PRE_SWITCH_TO)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG CurrentBrightnessLevel);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_SWITCH_CANCELED)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_SWITCH_CANCELED)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId,
    _In_ BOOLEAN MuxSwitchedToTarget);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_POST_SWITCH_AWAY)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_POST_SWITCH_AWAY)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_POST_SWITCH_TO_PHASE1)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_POST_SWITCH_TO_PHASE1)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG SwitchPrivateDataSize,
    _In_reads_bytes_(SwitchPrivateDataSize) PVOID pSwitchPrivateDataBuffer,
    _In_ GUID *pSwitchPrivateDataGUID);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_POST_SWITCH_TO_PHASE2)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_POST_SWITCH_TO_PHASE2)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId,
    _Out_ BOOLEAN *pWasPanelInPSR);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_UPDATE_STATE)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
VOID
(*DXGKDDI_DISPLAYMUX_UPDATE_STATE)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId,
    _In_ BOOLEAN MuxSwitchedToTarget);

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_REPORT_PRESENCE)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
VOID
(*DXGKDDI_DISPLAYMUX_REPORT_PRESENCE)(
    _In_ PVOID DriverContext,
    _In_ BOOLEAN SystemHasMux);

#define DISPLAYMUX_SWITCH_PRIVATE_DATA_MAX (1024 * 1024)

typedef struct _DXGK_DISPLAYMUX_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_DISPLAYMUX_GET_DRIVER_SUPPORT_LEVEL
        DxgkDdiDisplayMuxGetDriverSupportLevel;
    DXGKDDI_DISPLAYMUX_GET_RUNTIME_STATUS
        DxgkDdiDisplayMuxGetRuntimeStatus;
    DXGKDDI_DISPLAYMUX_PRE_SWITCH_AWAY
        DxgkDdiDisplayMuxPreSwitchAway;
    DXGKDDI_DISPLAYMUX_PRE_SWITCH_AWAY_GET_PRIVATE_DATA
        DxgkDdiDisplayMuxPreSwitchAwayGetPrivateData;
    DXGKDDI_DISPLAYMUX_PRE_SWITCH_TO
        DxgkDdiDisplayMuxPreSwitchTo;
    DXGKDDI_DISPLAYMUX_SWITCH_CANCELED
        DxgkDdiDisplayMuxSwitchCanceled;
    DXGKDDI_DISPLAYMUX_POST_SWITCH_AWAY
        DxgkDdiDisplayMuxPostSwitchAway;
    DXGKDDI_DISPLAYMUX_POST_SWITCH_TO_PHASE1
        DxgkDdiDisplayMuxPostSwitchToPhase1;
    DXGKDDI_DISPLAYMUX_POST_SWITCH_TO_PHASE2
        DxgkDdiDisplayMuxPostSwitchToPhase2;
    DXGKDDI_DISPLAYMUX_UPDATE_STATE
        DxgkDdiDisplayMuxUpdateState;
    DXGKDDI_DISPLAYMUX_REPORT_PRESENCE
        DxgkDdiDisplayMuxReportPresence;
} DXGK_DISPLAYMUX_INTERFACE, *PDXGK_DISPLAYMUX_INTERFACE;

#define DXGK_DISPLAYMUX_INTERFACE_VERSION_2 0x02

typedef
    _Function_class_DXGK_(DXGKDDI_DISPLAYMUX_SET_INTERNAL_PANEL_INFO)
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_DISPLAYMUX_SET_INTERNAL_PANEL_INFO)(
    _In_ PVOID DriverContext,
    _In_ ULONG VidPnTargetId,
    _In_ PDXGK_DISPLAYMUX_SET_INTERNAL_PANEL_INFO pInternalPanelInfo);

typedef struct _DXGK_DISPLAYMUX_INTERFACE_2
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_DISPLAYMUX_GET_DRIVER_SUPPORT_LEVEL
        DxgkDdiDisplayMuxGetDriverSupportLevel;
    DXGKDDI_DISPLAYMUX_GET_RUNTIME_STATUS
        DxgkDdiDisplayMuxGetRuntimeStatus;
    DXGKDDI_DISPLAYMUX_PRE_SWITCH_AWAY
        DxgkDdiDisplayMuxPreSwitchAway;
    DXGKDDI_DISPLAYMUX_PRE_SWITCH_AWAY_GET_PRIVATE_DATA
        DxgkDdiDisplayMuxPreSwitchAwayGetPrivateData;
    DXGKDDI_DISPLAYMUX_PRE_SWITCH_TO
        DxgkDdiDisplayMuxPreSwitchTo;
    DXGKDDI_DISPLAYMUX_SWITCH_CANCELED
        DxgkDdiDisplayMuxSwitchCanceled;
    DXGKDDI_DISPLAYMUX_POST_SWITCH_AWAY
        DxgkDdiDisplayMuxPostSwitchAway;
    DXGKDDI_DISPLAYMUX_POST_SWITCH_TO_PHASE1
        DxgkDdiDisplayMuxPostSwitchToPhase1;
    DXGKDDI_DISPLAYMUX_POST_SWITCH_TO_PHASE2
        DxgkDdiDisplayMuxPostSwitchToPhase2;
    DXGKDDI_DISPLAYMUX_UPDATE_STATE
        DxgkDdiDisplayMuxUpdateState;
    DXGKDDI_DISPLAYMUX_REPORT_PRESENCE
        DxgkDdiDisplayMuxReportPresence;
    DXGKDDI_DISPLAYMUX_SET_INTERNAL_PANEL_INFO
        DxgkDdiDisplayMuxSetInternalPanelInfo;
} DXGK_DISPLAYMUX_INTERFACE_2, *PDXGK_DISPLAYMUX_INTERFACE_2;

#define DXGK_FEATURE_INTERFACE_VERSION_1 0x1

typedef struct _DXGK_FEATURE_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKCB_ISFEATUREENABLED2 IsFeatureEnabled;
    DXGKCB_QUERYFEATUREINTERFACE QueryFeatureInterface;
} DXGK_FEATURE_INTERFACE, *PDXGK_FEATURE_INTERFACE;

typedef struct _DXGKDDI_FEATURE_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDXGKDDI_QUERYFEATURESUPPORT QueryFeatureSupport;
    PDXGKDDI_QUERYFEATUREINTERFACE QueryFeatureInterface;
} DXGKDDI_FEATURE_INTERFACE, *PDXGKDDI_FEATURE_INTERFACE;

#define DXGK_WAITWAKE_INTERFACE_VERSION_1 0x01

typedef
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
NTSTATUS
(*DXGKDDI_WAITWAKE_ARMING)(
    _In_ PVOID DriverContext);

typedef
    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
VOID
(*DXGKDDI_WAITWAKE_DISARMING)(
    _In_ PVOID DriverContext);

typedef struct _DXGK_WAITWAKE_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_WAITWAKE_ARMING DxgkDdiWaitWakeArming;
    DXGKDDI_WAITWAKE_DISARMING DxgkDdiWaitWakeDisarming;
} DXGK_WAITWAKE_INTERFACE, *PDXGK_WAITWAKE_INTERFACE;

#ifdef _WIN64
C_ASSERT(sizeof(DXGK_DISPLAYMUX_INTERFACE) == 0x78);
C_ASSERT(sizeof(DXGK_DISPLAYMUX_INTERFACE_2) == 0x80);
C_ASSERT(sizeof(DXGK_FEATURE_INTERFACE) == 0x30);
C_ASSERT(sizeof(DXGKDDI_FEATURE_INTERFACE) == 0x30);
C_ASSERT(sizeof(DXGK_WAITWAKE_INTERFACE) == 0x30);
#else
C_ASSERT(sizeof(DXGK_DISPLAYMUX_INTERFACE) == 0x3C);
C_ASSERT(sizeof(DXGK_DISPLAYMUX_INTERFACE_2) == 0x40);
C_ASSERT(sizeof(DXGK_FEATURE_INTERFACE) == 0x18);
C_ASSERT(sizeof(DXGKDDI_FEATURE_INTERFACE) == 0x18);
C_ASSERT(sizeof(DXGK_WAITWAKE_INTERFACE) == 0x18);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2 */

typedef enum
{
    DockStateUnsupported = 0,
    DockStateUnDocked = 1,
    DockStateDocked = 2,
    DockStateUnknown = 3,
} DOCKING_STATE;

typedef struct _DXGK_DEVICE_INFO
{
    PVOID MiniportDeviceContext;
    PDEVICE_OBJECT PhysicalDeviceObject;
    UNICODE_STRING DeviceRegistryPath;
    PCM_RESOURCE_LIST TranslatedResourceList;
    LARGE_INTEGER SystemMemorySize;
    PHYSICAL_ADDRESS HighestPhysicalAddress;
    PHYSICAL_ADDRESS AgpApertureBase;
    SIZE_T AgpApertureSize;
    DOCKING_STATE DockingState;
} DXGK_DEVICE_INFO, *PDXGK_DEVICE_INFO;


/* =========================================================================
 * DXGK_START_FLAGS / DXGK_START_INFO
 *
 * Passed as the first argument to DxgkDdiStartDevice.
 * =========================================================================
 */
typedef struct _DXGK_START_INFO
{
    ULONG               RequiredDmaQueueEntry;
    GUID                AdapterGuid;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    LUID                AdapterLuid;
#endif
} DXGK_START_INFO, *PDXGK_START_INFO;


/* =========================================================================
 * DXGK_EVENT_TYPE
 *
 * Used in DxgkDdiNotifyAcpiEvent to identify ACPI events.
 * =========================================================================
 */
typedef enum _DXGK_EVENT_TYPE
{
    DpEventType_Uninitialized   = 0,
    DpEventTypePowerStateChange = 1,
    DpEventTypeDisplaySwitch    = 2,
    DpEventTypeDockingEvent     = 3,
    DpEventTypeAcpiEvent        = 4,
    DpEventTypeResumeEvent      = 5,
    DpEventTypeDPCRoutineEvent  = 6,
} DXGK_EVENT_TYPE;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
/* WDDM 1.2 surprise-removal notification type.  PnP notification support
 * was added to the public contract for running devices in WDDM 2.0. */
typedef enum _DXGK_SURPRISE_REMOVAL_TYPE
{
    DxgkRemovalHibernation = 0,
    DxgkRemovalPnPNotify = 1,
} DXGK_SURPRISE_REMOVAL_TYPE;
#endif


/* =========================================================================
 * DISPLAY_ADAPTER_HW_ID
 *
 * Special UID passed to DxgkDdiSetPowerState to target the adapter itself
 * rather than a child display output.
 * =========================================================================
 */
#ifndef DISPLAY_ADAPTER_HW_ID
#define DISPLAY_ADAPTER_HW_ID   0xFFFFFFFFUL
#endif


/* =========================================================================
 * DxgkCb* service callback typedefs
 *
 * These are function pointers filled in by dxgkrnl in the DXGK_INTERFACE
 * structure that is handed to the miniport's DxgkDdiStartDevice callback.
 * =========================================================================
 */

typedef enum
{
    DxgkServicesAgp,
    DxgkServicesDebugReport,
    DxgkServicesTimedOperation,
    DxgkServicesSPB,
    DxgkServicesBDD,
    DxgkServicesFirmwareTable,
    DxgkServicesIDD,
    DxgkServicesFeature,
} DXGK_SERVICES;

typedef
    _Function_class_DXGK_(DXGKCB_EVAL_ACPI_METHOD)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_EVAL_ACPI_METHOD)(
    _In_ HANDLE DeviceHandle,
    _In_ ULONG DeviceUid,
    _In_reads_bytes_(AcpiInputSize)
        PACPI_EVAL_INPUT_BUFFER_COMPLEX AcpiInputBuffer,
    _In_range_(>=, sizeof(ACPI_EVAL_INPUT_BUFFER_COMPLEX))
        ULONG AcpiInputSize,
    _Out_writes_bytes_(AcpiOutputSize)
        PACPI_EVAL_OUTPUT_BUFFER AcpiOutputBuffer,
    _In_range_(>=, sizeof(ACPI_EVAL_OUTPUT_BUFFER))
        ULONG AcpiOutputSize);

/* Notify dxgkrnl of a GPU interrupt event (called from ISR at DIRQL). */
typedef
    _Function_class_DXGK_(DXGKCB_NOTIFY_INTERRUPT)
    _IRQL_requires_(HIGH_LEVEL)
VOID
(APIENTRY CALLBACK *DXGKCB_NOTIFY_INTERRUPT)(
    _In_ HANDLE hAdapter,
    IN_CONST_PDXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyInterruptData);

/* Compatibility spelling used by older in-tree miniports. */
typedef DXGKCB_NOTIFY_INTERRUPT PDXGKCB_NOTIFY_INTERRUPT;

/*
 * Notify dxgkrnl that the DPC triggered by NotifyInterrupt has run
 * (called from DPC at DISPATCH_LEVEL).
 */
typedef
    _Function_class_DXGK_(DXGKCB_NOTIFY_DPC)
    _IRQL_requires_(DISPATCH_LEVEL)
VOID
(APIENTRY CALLBACK *DXGKCB_NOTIFY_DPC)(
    _In_ HANDLE hAdapter);

/* Compatibility spelling used by older in-tree miniports. */
typedef DXGKCB_NOTIFY_DPC PDXGKCB_NOTIFY_DPC;

/* Retrieve hardware resource information populated during StartDevice. */
typedef
    _Function_class_DXGK_(DXGKCB_GET_DEVICE_INFORMATION)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_GET_DEVICE_INFORMATION)(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DEVICE_INFO DeviceInfo);

typedef DXGKCB_GET_DEVICE_INFORMATION PDXGKCB_GET_DEVICE_INFORMATION;

/* Report a child device connection/rotation status change. */
typedef
    _Function_class_DXGK_(DXGKCB_INDICATE_CHILD_STATUS)
    _IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_INDICATE_CHILD_STATUS)(
    _In_ HANDLE DeviceHandle,
    _In_ PDXGK_CHILD_STATUS ChildStatus);

typedef DXGKCB_INDICATE_CHILD_STATUS PDXGKCB_INDICATE_CHILD_STATUS;

typedef
    _Function_class_DXGK_(DXGKCB_MAP_MEMORY)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_MAP_MEMORY)(
    _In_ HANDLE DeviceHandle,
    _In_ PHYSICAL_ADDRESS TranslatedAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN InIoSpace,
    _In_ BOOLEAN MapToUserMode,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _Outptr_ PVOID *VirtualAddress);

typedef DXGKCB_MAP_MEMORY PDXGKCB_MAP_MEMORY;

typedef
    _Function_class_DXGK_(DXGKCB_QUERY_SERVICES)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_QUERY_SERVICES)(
    _In_ HANDLE DeviceHandle,
    _In_ DXGK_SERVICES ServicesType,
    _Inout_ PINTERFACE Interface);

typedef
    _Function_class_DXGK_(DXGKCB_QUEUE_DPC)
    _Success_(return != 0)
BOOLEAN
(APIENTRY *DXGKCB_QUEUE_DPC)(
    _In_ HANDLE DeviceHandle);

typedef DXGKCB_QUEUE_DPC PDXGKCB_QUEUE_DPC;

/*
 * Synchronize a routine with the GPU interrupt service routine.
 * Equivalent to KeSynchronizeExecution for the adapter's interrupt.
 */
typedef
    _Function_class_DXGK_(DXGKCB_SYNCHRONIZE_EXECUTION)
    _IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_SYNCHRONIZE_EXECUTION)(
    _In_ HANDLE DeviceHandle,
    _In_ PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    _In_ PVOID Context,
    _In_ ULONG MessageNumber,
    _Out_ PBOOLEAN ReturnValue);

typedef DXGKCB_SYNCHRONIZE_EXECUTION PDXGKCB_SYNCHRONIZE_EXECUTION;

/*
 * Acquire ownership of the post-display information (DXGK_DISPLAY_INFORMATION)
 * from the system firmware / boot graphics driver.
 * Available on Win8+ (WDDM 1.2); may be NULL on Vista/Win7.
 */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
typedef
    _Function_class_DXGK_(DXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP)(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo);

typedef DXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP
    PDXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
typedef enum _DXGK_FRAMEBUFFER_STATE
{
    FrameBufferStateUnknown = 0,
    FrameBufferStateInitializedByFirmware = 1,
    FrameBufferStateInitializedByDriver = 2,
} DXGK_FRAMEBUFFER_STATE;

typedef struct _DXGK_DISPLAY_OWNERSHIP_FLAGS
{
    union
    {
        struct
        {
            DXGK_FRAMEBUFFER_STATE FrameBufferState : 4;
        };
        UINT Value;
    };
} DXGK_DISPLAY_OWNERSHIP_FLAGS, *PDXGK_DISPLAY_OWNERSHIP_FLAGS;

typedef
    _Function_class_DXGK_(DXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP2)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP2)(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo,
    _Out_ PDXGK_DISPLAY_OWNERSHIP_FLAGS Flags);
#endif

/* Unmap a range previously mapped by DxgkCbMapMemory. */
typedef
    _Function_class_DXGK_(DXGKCB_UNMAP_MEMORY)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_UNMAP_MEMORY)(
    _In_ HANDLE DeviceHandle,
    _In_ PVOID VirtualAddress);

typedef DXGKCB_UNMAP_MEMORY PDXGKCB_UNMAP_MEMORY;

/*
 * Read from a device configuration space or expansion ROM.
 * DataType: DXGK_WHICHSPACE_CONFIG, _BRIDGE, _MCH, or _ROM.
 */
typedef
    _Function_class_DXGK_(DXGKCB_READ_DEVICE_SPACE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_READ_DEVICE_SPACE)(
    _In_ HANDLE DeviceHandle,
    _In_ ULONG DataType,
    _Out_writes_bytes_to_(Length, *BytesRead) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _Out_ PULONG BytesRead);

typedef DXGKCB_READ_DEVICE_SPACE PDXGKCB_READ_DEVICE_SPACE;

/*
 * Write to a device configuration space.
 * DataType: DXGK_WHICHSPACE_CONFIG, _BRIDGE, _MCH, or _ROM.
 */
typedef
    _Function_class_DXGK_(DXGKCB_WRITE_DEVICE_SPACE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_WRITE_DEVICE_SPACE)(
    _In_ HANDLE DeviceHandle,
    _In_ ULONG DataType,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _Out_ _Out_range_(<=, Length) PULONG BytesWritten);

typedef DXGKCB_WRITE_DEVICE_SPACE PDXGKCB_WRITE_DEVICE_SPACE;

typedef
    _Function_class_DXGK_(DXGKCB_IS_DEVICE_PRESENT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_IS_DEVICE_PRESENT)(
    _In_ HANDLE DeviceHandle,
    _In_ PPCI_DEVICE_PRESENCE_PARAMETERS DevicePresenceParameters,
    _Out_ PBOOLEAN DevicePresent);

typedef
    _Function_class_DXGK_(DXGKCB_LOG_ETW_EVENT)
    _When_(EventBufferSize > 256, _IRQL_requires_(PASSIVE_LEVEL))
VOID
(APIENTRY *DXGKCB_LOG_ETW_EVENT)(
    _In_ CONST LPCGUID EventGuid,
    _In_ UCHAR Type,
    _In_ USHORT EventBufferSize,
    _In_reads_bytes_(EventBufferSize) PVOID EventBuffer);

typedef
    _Function_class_DXGK_(DXGKCB_EXCLUDE_ADAPTER_ACCESS)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKCB_EXCLUDE_ADAPTER_ACCESS)(
    _In_ HANDLE DeviceHandle,
    _In_ ULONG Attributes,
    _In_ DXGKDDI_PROTECTED_CALLBACK DxgkProtectedCallback,
    _In_ PVOID ProtectedCallbackContext);

/* DXGK_WHICHSPACE constants for Read/WriteDeviceSpace */
#ifndef DXGK_WHICHSPACE_BRIDGE
#define DXGK_WHICHSPACE_BRIDGE   0x00000000
#define DXGK_WHICHSPACE_CONFIG   0x00000001
#define DXGK_WHICHSPACE_MCH      0x00000002
#define DXGK_WHICHSPACE_ROM      0x00000003
#endif


/* =========================================================================
 * VidPN interface version enum
 * =========================================================================
 */
typedef enum _DXGK_VIDPN_INTERFACE_VERSION
{
    DXGK_VIDPN_INTERFACE_VERSION_UNINITIALIZED = 0,
    DXGK_VIDPN_INTERFACE_VERSION_V1            = 1,
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    DXGK_VIDPN_INTERFACE_VERSION_V2            = 2,
#endif
} DXGK_VIDPN_INTERFACE_VERSION;

/* =========================================================================
 * Monitor interface version enum
 * =========================================================================
 */
typedef enum _DXGK_MONITOR_INTERFACE_VERSION
{
    DXGK_MONITOR_INTERFACE_VERSION_UNINITIALIZED = 0,
    DXGK_MONITOR_INTERFACE_VERSION_V1            = 1,
    DXGK_MONITOR_INTERFACE_VERSION_V2            = 2,
} DXGK_MONITOR_INTERFACE_VERSION;

typedef struct _DXGK_MONITOR_INTERFACE DXGK_MONITOR_INTERFACE;

/*
 * VidPN handle typedefs (D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTOPOLOGY, etc.)
 * are declared in d3dkmdt.h via DECLARE_HANDLE(); do not redefine here.
 */

/* =========================================================================
 * DXGK_VIDPNTOPOLOGY_INTERFACE
 *
 * Function table returned by DXGK_VIDPN_INTERFACE::pfnGetTopology.
 * =========================================================================
 */
typedef struct _DXGK_VIDPNTOPOLOGY_INTERFACE
{
    NTSTATUS (APIENTRY *pfnGetNumPaths)(
        _In_  D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
        _Out_ SIZE_T*                pNumPaths);

    NTSTATUS (APIENTRY *pfnGetNumPathsFromSource)(
        _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
        _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
        _Out_ SIZE_T*                               pNumPathsFromSource);

    NTSTATUS (APIENTRY *pfnEnumPathTargetsFromSource)(
        _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
        _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
        _In_  D3DKMDT_VIDPN_PRESENT_PATH_INDEX      PathIndex,
        _Out_ D3DDDI_VIDEO_PRESENT_TARGET_ID*       pVidPnTargetId);

    NTSTATUS (APIENTRY *pfnGetPathSourceFromTarget)(
        _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
        _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID        VidPnTargetId,
        _Out_ D3DDDI_VIDEO_PRESENT_SOURCE_ID*       pVidPnSourceId);

    NTSTATUS (APIENTRY *pfnAcquirePathInfo)(
        _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
        _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
        _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID        VidPnTargetId,
        _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppVidPnPresentPathInfo);

    NTSTATUS (APIENTRY *pfnAcquireFirstPathInfo)(
        _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
        _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppFirstVidPnPresentPathInfo);

    NTSTATUS (APIENTRY *pfnAcquireNextPathInfo)(
        _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
        _In_  CONST D3DKMDT_VIDPN_PRESENT_PATH*    pVidPnPresentPathInfo,
        _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppNextVidPnPresentPathInfo);

    NTSTATUS (APIENTRY *pfnUpdatePathSupportInfo)(
        _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
        _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH*     pVidPnPresentPathInfo);

    NTSTATUS (APIENTRY *pfnReleasePathInfo)(
        _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
        _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH*     pVidPnPresentPathInfo);

    NTSTATUS (APIENTRY *pfnCreateNewPathInfo)(
        _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
        _Out_ D3DKMDT_VIDPN_PRESENT_PATH**         ppNewVidPnPresentPathInfo);

    NTSTATUS (APIENTRY *pfnAddPath)(
        _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
        _In_ D3DKMDT_VIDPN_PRESENT_PATH*           pVidPnPresentPath);

    NTSTATUS (APIENTRY *pfnRemovePath)(
        _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
        _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID         VidPnSourceId,
        _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID         VidPnTargetId);
} DXGK_VIDPNTOPOLOGY_INTERFACE;

/* =========================================================================
 * DXGK_VIDPNSOURCEMODESET_INTERFACE
 *
 * Function table for manipulating a VidPN source mode set.
 * =========================================================================
 */
typedef struct _DXGK_VIDPNSOURCEMODESET_INTERFACE
{
    NTSTATUS (APIENTRY *pfnGetNumModes)(
        _In_  D3DKMDT_HVIDPNSOURCEMODESET           hVidPnSourceModeSet,
        _Out_ CONST SIZE_T*                         pNumModes);

    NTSTATUS (APIENTRY *pfnAcquireFirstModeInfo)(
        _In_  D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
        _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**             ppFirstVidPnSourceModeInfo);

    NTSTATUS (APIENTRY *pfnAcquireNextModeInfo)(
        _In_  D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
        _In_  CONST D3DKMDT_VIDPN_SOURCE_MODE*              pVidPnSourceModeInfo,
        _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**             ppNextVidPnSourceModeInfo);

    NTSTATUS (APIENTRY *pfnAcquirePinnedModeInfo)(
        _In_  D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
        _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**             ppPinnedVidPnSourceModeInfo);

    NTSTATUS (APIENTRY *pfnReleaseModeInfo)(
        _In_ D3DKMDT_HVIDPNSOURCEMODESET                    hVidPnSourceModeSet,
        _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE*               pVidPnSourceModeInfo);

    NTSTATUS (APIENTRY *pfnCreateNewModeInfo)(
        _In_  D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
        _Out_ D3DKMDT_VIDPN_SOURCE_MODE**                   ppNewVidPnSourceModeInfo);

    NTSTATUS (APIENTRY *pfnAddMode)(
        _In_ D3DKMDT_HVIDPNSOURCEMODESET                    hVidPnSourceModeSet,
        _In_ D3DKMDT_VIDPN_SOURCE_MODE*                     pVidPnSourceModeInfo);

    NTSTATUS (APIENTRY *pfnPinMode)(
        _In_ D3DKMDT_HVIDPNSOURCEMODESET                    hVidPnSourceModeSet,
        _In_ D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID           VidPnSourceModeId);
} DXGK_VIDPNSOURCEMODESET_INTERFACE;

/* =========================================================================
 * DXGK_VIDPNTARGETMODESET_INTERFACE
 *
 * Function table for manipulating a VidPN target mode set.
 * =========================================================================
 */
typedef struct _DXGK_VIDPNTARGETMODESET_INTERFACE
{
    NTSTATUS (APIENTRY *pfnGetNumModes)(
        _In_  D3DKMDT_HVIDPNTARGETMODESET           hVidPnTargetModeSet,
        _Out_ CONST SIZE_T*                         pNumModes);

    NTSTATUS (APIENTRY *pfnAcquireFirstModeInfo)(
        _In_  D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
        _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**             ppFirstVidPnTargetModeInfo);

    NTSTATUS (APIENTRY *pfnAcquireNextModeInfo)(
        _In_  D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
        _In_  CONST D3DKMDT_VIDPN_TARGET_MODE*              pVidPnTargetModeInfo,
        _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**             ppNextVidPnTargetModeInfo);

    NTSTATUS (APIENTRY *pfnAcquirePinnedModeInfo)(
        _In_  D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
        _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**             ppPinnedVidPnTargetModeInfo);

    NTSTATUS (APIENTRY *pfnReleaseModeInfo)(
        _In_ D3DKMDT_HVIDPNTARGETMODESET                    hVidPnTargetModeSet,
        _In_ CONST D3DKMDT_VIDPN_TARGET_MODE*               pVidPnTargetModeInfo);

    NTSTATUS (APIENTRY *pfnCreateNewModeInfo)(
        _In_  D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
        _Out_ D3DKMDT_VIDPN_TARGET_MODE**                   ppNewVidPnTargetModeInfo);

    NTSTATUS (APIENTRY *pfnAddMode)(
        _In_ D3DKMDT_HVIDPNTARGETMODESET                    hVidPnTargetModeSet,
        _In_ D3DKMDT_VIDPN_TARGET_MODE*                     pVidPnTargetModeInfo);

    NTSTATUS (APIENTRY *pfnPinMode)(
        _In_ D3DKMDT_HVIDPNTARGETMODESET                    hVidPnTargetModeSet,
        _In_ D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID           VidPnTargetModeId);
} DXGK_VIDPNTARGETMODESET_INTERFACE;

/* Monitor-source-mode-set interface. */
typedef _Out_ SIZE_T* CONST OUT_PSIZE_T_CONST;
typedef _Out_ UINT* OUT_PUINT;
typedef _In_ CONST D3DDDI_VIDEO_PRESENT_TARGET_ID
    IN_CONST_D3DDDI_VIDEO_PRESENT_TARGET_ID;
typedef _In_ CONST D3DKMDT_HMONITORSOURCEMODESET
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET;
typedef _In_ D3DKMDT_MONITOR_SOURCE_MODE* CONST
    IN_PD3DKMDT_MONITOR_SOURCE_MODE_CONST;
typedef _In_ CONST D3DKMDT_MONITOR_SOURCE_MODE* CONST
    IN_CONST_PD3DKMDT_MONITOR_SOURCE_MODE_CONST;
typedef _Outptr_ D3DKMDT_MONITOR_SOURCE_MODE**
    DEREF_OUT_PPD3DKMDT_MONITOR_SOURCE_MODE;
typedef _Outptr_ CONST D3DKMDT_MONITOR_SOURCE_MODE**
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_SOURCE_MODE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORSOURCEMODESET_GETNUMMODES)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORSOURCEMODESET_GETNUMMODES)(
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
    OUT_PSIZE_T_CONST pNumMonitorSourceModes);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORSOURCEMODESET_ACQUIREPREFERREDMODEINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORSOURCEMODESET_ACQUIREPREFERREDMODEINFO)(
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_SOURCE_MODE
        ppFirstMonitorSourceModeInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORSOURCEMODESET_ACQUIREFIRSTMODEINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORSOURCEMODESET_ACQUIREFIRSTMODEINFO)(
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_SOURCE_MODE
        ppFirstMonitorSourceModeInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORSOURCEMODESET_ACQUIRENEXTMODEINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORSOURCEMODESET_ACQUIRENEXTMODEINFO)(
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
    IN_CONST_PD3DKMDT_MONITOR_SOURCE_MODE_CONST pMonitorSourceModeInfo,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_SOURCE_MODE
        ppNextMonitorSourceModeInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORSOURCEMODESET_CREATENEWMODEINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORSOURCEMODESET_CREATENEWMODEINFO)(
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
    DEREF_OUT_PPD3DKMDT_MONITOR_SOURCE_MODE ppNewMonitorSourceModeInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORSOURCEMODESET_ADDMODE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORSOURCEMODESET_ADDMODE)(
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
    IN_PD3DKMDT_MONITOR_SOURCE_MODE_CONST pMonitorSourceModeInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORSOURCEMODESET_RELEASEMODEINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORSOURCEMODESET_RELEASEMODEINFO)(
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
    IN_CONST_PD3DKMDT_MONITOR_SOURCE_MODE_CONST pMonitorSourceModeInfo);

typedef struct _DXGK_MONITORSOURCEMODESET_INTERFACE
{
    DXGKDDI_MONITORSOURCEMODESET_GETNUMMODES pfnGetNumModes;
    DXGKDDI_MONITORSOURCEMODESET_ACQUIREPREFERREDMODEINFO
        pfnAcquirePreferredModeInfo;
    DXGKDDI_MONITORSOURCEMODESET_ACQUIREFIRSTMODEINFO
        pfnAcquireFirstModeInfo;
    DXGKDDI_MONITORSOURCEMODESET_ACQUIRENEXTMODEINFO
        pfnAcquireNextModeInfo;
    DXGKDDI_MONITORSOURCEMODESET_CREATENEWMODEINFO pfnCreateNewModeInfo;
    DXGKDDI_MONITORSOURCEMODESET_ADDMODE pfnAddMode;
    DXGKDDI_MONITORSOURCEMODESET_RELEASEMODEINFO pfnReleaseModeInfo;
} DXGK_MONITORSOURCEMODESET_INTERFACE;

/* Monitor-frequency-range-set interface. */
typedef _In_ CONST D3DKMDT_HMONITORFREQUENCYRANGESET
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET;
typedef _In_ CONST D3DKMDT_MONITOR_FREQUENCY_RANGE* CONST
    IN_CONST_PD3DKMDT_MONITOR_FREQUENCY_RANGE_CONST;
typedef _Outptr_ CONST D3DKMDT_MONITOR_FREQUENCY_RANGE**
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_FREQUENCY_RANGE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORFREQUENCYRANGESET_GETNUMFREQUENCYRANGES)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORFREQUENCYRANGESET_GETNUMFREQUENCYRANGES)(
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET hMonitorFrequencyRangeSet,
    OUT_PSIZE_T_CONST pNumMonitorFrequencyRanges);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORFREQUENCYRANGESET_ACQUIREFIRSTFREQUENCYRANGEINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORFREQUENCYRANGESET_ACQUIREFIRSTFREQUENCYRANGEINFO)(
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET hMonitorFrequencyRangeSet,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_FREQUENCY_RANGE
        ppFirstMonitorFrequencyRangeInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORFREQUENCYRANGESET_ACQUIRENEXTFREQUENCYRANGEINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORFREQUENCYRANGESET_ACQUIRENEXTFREQUENCYRANGEINFO)(
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET hMonitorFrequencyRangeSet,
    IN_CONST_PD3DKMDT_MONITOR_FREQUENCY_RANGE_CONST
        pMonitorFrequencyRangeInfo,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_FREQUENCY_RANGE
        ppNextMonitorFrequencyRangeInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORFREQUENCYRANGESET_RELEASEFREQUENCYRANGEINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORFREQUENCYRANGESET_RELEASEFREQUENCYRANGEINFO)(
    IN_CONST_D3DKMDT_HMONITORFREQUENCYRANGESET hMonitorFrequencyRangeSet,
    IN_CONST_PD3DKMDT_MONITOR_FREQUENCY_RANGE_CONST
        pMonitorFrequencyRangeInfo);

typedef struct _DXGK_MONITORFREQUENCYRANGESET_INTERFACE
{
    DXGKDDI_MONITORFREQUENCYRANGESET_GETNUMFREQUENCYRANGES
        pfnGetNumFrequencyRanges;
    DXGKDDI_MONITORFREQUENCYRANGESET_ACQUIREFIRSTFREQUENCYRANGEINFO
        pfnAcquireFirstFrequencyRangeInfo;
    DXGKDDI_MONITORFREQUENCYRANGESET_ACQUIRENEXTFREQUENCYRANGEINFO
        pfnAcquireNextFrequencyRangeInfo;
    DXGKDDI_MONITORFREQUENCYRANGESET_RELEASEFREQUENCYRANGEINFO
        pfnReleaseFrequencyRangeInfo;
} DXGK_MONITORFREQUENCYRANGESET_INTERFACE;

/* Monitor-descriptor-set interface. */
typedef _In_ CONST D3DKMDT_HMONITORDESCRIPTORSET
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET;
typedef _In_ CONST D3DKMDT_MONITOR_DESCRIPTOR* CONST
    IN_CONST_PD3DKMDT_MONITOR_DESCRIPTOR_CONST;
typedef _Outptr_ CONST D3DKMDT_MONITOR_DESCRIPTOR**
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_DESCRIPTOR;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORDESCRIPTORSET_GETNUMDESCRIPTORS)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORDESCRIPTORSET_GETNUMDESCRIPTORS)(
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET hMonitorDescriptorSet,
    OUT_PSIZE_T_CONST pNumMonitorDescriptors);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORDESCRIPTORSET_ACQUIREFIRSTDESCRIPTORINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORDESCRIPTORSET_ACQUIREFIRSTDESCRIPTORINFO)(
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET hMonitorDescriptorSet,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_DESCRIPTOR
        ppFirstMonitorDescriptorInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORDESCRIPTORSET_ACQUIRENEXTDESCRIPTORINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORDESCRIPTORSET_ACQUIRENEXTDESCRIPTORINFO)(
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET hMonitorDescriptorSet,
    IN_CONST_PD3DKMDT_MONITOR_DESCRIPTOR_CONST pMonitorDescriptorInfo,
    DEREF_OUT_CONST_PPD3DKMDT_MONITOR_DESCRIPTOR
        ppNextMonitorDescriptorInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITORDESCRIPTORSET_RELEASEDESCRIPTORINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITORDESCRIPTORSET_RELEASEDESCRIPTORINFO)(
    IN_CONST_D3DKMDT_HMONITORDESCRIPTORSET hMonitorDescriptorSet,
    IN_CONST_PD3DKMDT_MONITOR_DESCRIPTOR_CONST pMonitorDescriptorInfo);

typedef struct _DXGK_MONITORDESCRIPTORSET_INTERFACE
{
    DXGKDDI_MONITORDESCRIPTORSET_GETNUMDESCRIPTORS pfnGetNumDescriptors;
    DXGKDDI_MONITORDESCRIPTORSET_ACQUIREFIRSTDESCRIPTORINFO
        pfnAcquireFirstDescriptorInfo;
    DXGKDDI_MONITORDESCRIPTORSET_ACQUIRENEXTDESCRIPTORINFO
        pfnAcquireNextDescriptorInfo;
    DXGKDDI_MONITORDESCRIPTORSET_RELEASEDESCRIPTORINFO
        pfnReleaseDescriptorInfo;
} DXGK_MONITORDESCRIPTORSET_INTERFACE;

/* Top-level monitor interfaces. */
typedef _In_ CONST D3DKMDT_ADAPTER IN_CONST_D3DKMDT_ADAPTER;
typedef _Out_ D3DKMDT_HMONITORDESCRIPTORSET*
    OUT_PD3DKMDT_HMONITORDESCRIPTORSET;
typedef _Out_ D3DKMDT_HMONITORSOURCEMODESET*
    OUT_PD3DKMDT_HMONITORSOURCEMODESET;
typedef _Out_ D3DKMDT_HMONITORFREQUENCYRANGESET*
    OUT_PD3DKMDT_HMONITORFREQUENCYRANGESET;
typedef _Outptr_ CONST DXGK_MONITORSOURCEMODESET_INTERFACE**
    DEREF_OUT_CONST_PPDXGK_MONITORSOURCEMODESET_INTERFACE;
typedef _Outptr_ CONST DXGK_MONITORFREQUENCYRANGESET_INTERFACE**
    DEREF_OUT_CONST_PPDXGK_MONITORFREQUENCYRANGESET_INTERFACE;
typedef _Outptr_ CONST DXGK_MONITORDESCRIPTORSET_INTERFACE**
    DEREF_OUT_CONST_PPDXGK_MONITORDESCRIPTORSET_INTERFACE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITOR_ACQUIREMONITORSOURCEMODESET)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITOR_ACQUIREMONITORSOURCEMODESET)(
    IN_CONST_D3DKMDT_ADAPTER hAdapter,
    IN_CONST_D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    OUT_PD3DKMDT_HMONITORSOURCEMODESET phMonitorSourceModeSet,
    DEREF_OUT_CONST_PPDXGK_MONITORSOURCEMODESET_INTERFACE
        ppMonitorSourceModeSetInterface);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITOR_RELEASEMONITORSOURCEMODESET)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITOR_RELEASEMONITORSOURCEMODESET)(
    IN_CONST_D3DKMDT_ADAPTER hAdapter,
    IN_CONST_D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITOR_GETMONITORFREQUENCYRANGESET)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITOR_GETMONITORFREQUENCYRANGESET)(
    IN_CONST_D3DKMDT_ADAPTER hAdapter,
    IN_CONST_D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    OUT_PD3DKMDT_HMONITORFREQUENCYRANGESET phMonitorFrequencyRangeSet,
    DEREF_OUT_CONST_PPDXGK_MONITORFREQUENCYRANGESET_INTERFACE
        ppMonitorFrequencyRangeSetInterface);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITOR_GETMONITORDESCRIPTORSET)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITOR_GETMONITORDESCRIPTORSET)(
    IN_CONST_D3DKMDT_ADAPTER hAdapter,
    IN_CONST_D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    OUT_PD3DKMDT_HMONITORDESCRIPTORSET phMonitorDescriptorSet,
    DEREF_OUT_CONST_PPDXGK_MONITORDESCRIPTORSET_INTERFACE
        ppMonitorDescriptorSetInterface);

struct _DXGK_MONITOR_INTERFACE
{
    DXGK_MONITOR_INTERFACE_VERSION Version;
    DXGKDDI_MONITOR_ACQUIREMONITORSOURCEMODESET
        pfnAcquireMonitorSourceModeSet;
    DXGKDDI_MONITOR_RELEASEMONITORSOURCEMODESET
        pfnReleaseMonitorSourceModeSet;
    DXGKDDI_MONITOR_GETMONITORFREQUENCYRANGESET
        pfnGetMonitorFrequencyRangeSet;
    DXGKDDI_MONITOR_GETMONITORDESCRIPTORSET
        pfnGetMonitorDescriptorSet;
};

typedef _In_ CONST DXGK_TARGETMODE_DETAIL_TIMING*
    IN_CONST_PDXGK_TARGETMODE_DETAIL_TIMING;
typedef DXGK_TARGETMODE_DETAIL_TIMING**
    DEREF_ECOUNT_PPDXGK_TARGETMODE_DETAIL_TIMING;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITOR_GETADDITIONALMONITORMODESET)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITOR_GETADDITIONALMONITORMODESET)(
    IN_CONST_D3DKMDT_ADAPTER hAdapter,
    IN_CONST_D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    OUT_PUINT pNumberModes,
    _At_(*ppAdditionalModesSet, _Inout_updates_(*pNumberModes))
    DEREF_ECOUNT_PPDXGK_TARGETMODE_DETAIL_TIMING ppAdditionalModesSet);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_MONITOR_RELEASEADDITIONALMONITORMODESET)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY *DXGKDDI_MONITOR_RELEASEADDITIONALMONITORMODESET)(
    IN_CONST_D3DKMDT_ADAPTER hAdapter,
    IN_CONST_D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    IN_CONST_PDXGK_TARGETMODE_DETAIL_TIMING pAdditionalModesSet);

typedef struct _DXGK_MONITOR_INTERFACE_V2
{
    DXGK_MONITOR_INTERFACE_VERSION Version;
    DXGKDDI_MONITOR_ACQUIREMONITORSOURCEMODESET
        pfnAcquireMonitorSourceModeSet;
    DXGKDDI_MONITOR_RELEASEMONITORSOURCEMODESET
        pfnReleaseMonitorSourceModeSet;
    DXGKDDI_MONITOR_GETMONITORFREQUENCYRANGESET
        pfnGetMonitorFrequencyRangeSet;
    DXGKDDI_MONITOR_GETMONITORDESCRIPTORSET
        pfnGetMonitorDescriptorSet;
    DXGKDDI_MONITOR_GETADDITIONALMONITORMODESET
        pfnGetAdditionalMonitorModeSet;
    DXGKDDI_MONITOR_RELEASEADDITIONALMONITORMODESET
        pfnReleaseAdditionalMonitorModeSet;
} DXGK_MONITOR_INTERFACE_V2;

/* =========================================================================
 * DXGK_VIDPN_INTERFACE
 *
 * Top-level VidPN interface returned by DxgkCbQueryVidPnInterface.
 * =========================================================================
 */
typedef struct _DXGK_VIDPN_INTERFACE
{
    DXGK_VIDPN_INTERFACE_VERSION Version;

    NTSTATUS (APIENTRY *pfnGetTopology)(
        _In_  D3DKMDT_HVIDPN                               hVidPn,
        _Out_ D3DKMDT_HVIDPNTOPOLOGY*                      phVidPnTopology,
        _Out_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE**         ppVidPnTopologyInterface);

    NTSTATUS (APIENTRY *pfnAcquireSourceModeSet)(
        _In_  D3DKMDT_HVIDPN                               hVidPn,
        _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
        _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                 phVidPnSourceModeSet,
        _Out_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**    ppVidPnSourceModeSetInterface);

    NTSTATUS (APIENTRY *pfnReleaseSourceModeSet)(
        _In_ D3DKMDT_HVIDPN                                hVidPn,
        _In_ D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet);

    NTSTATUS (APIENTRY *pfnCreateNewSourceModeSet)(
        _In_  D3DKMDT_HVIDPN                               hVidPn,
        _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
        _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                 phVidPnSourceModeSet,
        _Out_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**    ppVidPnSourceModeSetInterface);

    NTSTATUS (APIENTRY *pfnAssignSourceModeSet)(
        _In_ D3DKMDT_HVIDPN                                hVidPn,
        _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID                 VidPnSourceId,
        _In_ D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet);

    NTSTATUS (APIENTRY *pfnAssignMultisamplingMethodSet)(
        _In_ D3DKMDT_HVIDPN                                hVidPn,
        _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID                 VidPnSourceId,
        _In_ CONST D3DDDI_MULTISAMPLINGMETHOD*             pMultisamplingMethod);

    NTSTATUS (APIENTRY *pfnAcquireTargetModeSet)(
        _In_  D3DKMDT_HVIDPN                               hVidPn,
        _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID                VidPnTargetId,
        _Out_ D3DKMDT_HVIDPNTARGETMODESET*                 phVidPnTargetModeSet,
        _Out_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE**    ppVidPnTargetModeSetInterface);

    NTSTATUS (APIENTRY *pfnReleaseTargetModeSet)(
        _In_ D3DKMDT_HVIDPN                                hVidPn,
        _In_ D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet);

    NTSTATUS (APIENTRY *pfnCreateNewTargetModeSet)(
        _In_  D3DKMDT_HVIDPN                               hVidPn,
        _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID                VidPnTargetId,
        _Out_ D3DKMDT_HVIDPNTARGETMODESET*                 phVidPnTargetModeSet,
        _Out_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE**    ppVidPnTargetModeSetInterface);

    NTSTATUS (APIENTRY *pfnAssignTargetModeSet)(
        _In_ D3DKMDT_HVIDPN                                hVidPn,
        _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID                 VidPnTargetId,
        _In_ D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet);
} DXGK_VIDPN_INTERFACE;

/* =========================================================================
 * DxgkCbQueryVidPnInterface / DxgkCbQueryMonitorInterface callback types
 * =========================================================================
 */
typedef _In_ CONST D3DKMDT_HVIDPN
    IN_CONST_D3DKMDT_HVIDPN;
typedef _In_ CONST DXGK_VIDPN_INTERFACE_VERSION
    IN_CONST_DXGK_VIDPN_INTERFACE_VERSION;
typedef _Outptr_ CONST DXGK_VIDPN_INTERFACE**
    DEREF_OUT_CONST_PPDXGK_VIDPN_INTERFACE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_QUERYVIDPNINTERFACE)
    _IRQL_requires_max_(APC_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_QUERYVIDPNINTERFACE)(
    IN_CONST_D3DKMDT_HVIDPN hVidPn,
    IN_CONST_DXGK_VIDPN_INTERFACE_VERSION VidPnInterfaceVersion,
    DEREF_OUT_CONST_PPDXGK_VIDPN_INTERFACE ppVidPnInterface);

typedef _In_ CONST DXGK_MONITOR_INTERFACE_VERSION
    IN_CONST_DXGK_MONITOR_INTERFACE_VERSION;
typedef _Outptr_ CONST DXGK_MONITOR_INTERFACE**
    DEREF_OUT_CONST_PPDXGK_MONITOR_INTERFACE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_QUERYMONITORINTERFACE)
    _IRQL_requires_max_(APC_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_QUERYMONITORINTERFACE)(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGK_MONITOR_INTERFACE_VERSION MonitorInterfaceVersion,
    DEREF_OUT_CONST_PPDXGK_MONITOR_INTERFACE ppMonitorInterface);

/* =========================================================================
 * DOD-specific types (DXGKARG_PRESENT_DISPLAYONLY, etc.)
 * =========================================================================
 */
typedef union _D3DKMT_PRESENT_DISPLAY_ONLY_FLAGS
{
    struct
    {
        UINT Rotate : 1;
        UINT Reserved : 31;
    };
    UINT Value;
} D3DKMT_PRESENT_DISPLAY_ONLY_FLAGS, *PD3DKMT_PRESENT_DISPLAY_ONLY_FLAGS;

typedef VOID
(APIENTRY *DXGKCB_PRESENT_DISPLAYONLY_PROGRESS)(
    _In_ HANDLE hAdapter,
    _In_ CONST DXGKARGCB_PRESENT_DISPLAYONLY_PROGRESS *Progress);

typedef struct _DXGKARG_PRESENT_DISPLAYONLY
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID               VidPnSourceId;
    VOID*                                        pSource;
    ULONG                                        BytesPerPixel;
    LONG                                         Pitch;
    D3DKMT_PRESENT_DISPLAY_ONLY_FLAGS            Flags;
    ULONG                                        NumMoves;
    D3DKMT_MOVE_RECT*                            pMoves;
    ULONG                                        NumDirtyRects;
    RECT*                                        pDirtyRect;
    DXGKCB_PRESENT_DISPLAYONLY_PROGRESS          pfnPresentDisplayOnlyProgress;
} DXGKARG_PRESENT_DISPLAYONLY;

typedef union _DXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS
{
    struct
    {
        UINT Reserved : 32;
    };
    UINT Value;
} DXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS, *PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS;

/*
 * DXGKARG_RECOMMENDMONITORMODES is defined in d3dkmddi.h with the same
 * layout; do not redefine here.
 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)

typedef struct _DXGK_DIAGNOSTIC_CATEGORIES
{
    union
    {
        struct
        {
            UINT Notifications : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
            UINT Progressions : 1;
            UINT Reserved     : 30;
#else
            UINT Reserved     : 31;
#endif
        };
        UINT Value;
    };
} DXGK_DIAGNOSTIC_CATEGORIES;

#define DXGK_DIAGCAT_NOTIFICATIONS_BIT  0
#define DXGK_DIAGCAT_NOTIFICATIONS_MASK (1 << DXGK_DIAGCAT_NOTIFICATIONS_BIT)

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
#define DXGK_DIAGCAT_PROGRESSIONS_BIT  1
#define DXGK_DIAGCAT_PROGRESSIONS_MASK (1 << DXGK_DIAGCAT_PROGRESSIONS_BIT)
#define DXGK_DIAGCAT_BITCOUNT          2
#else
#define DXGK_DIAGCAT_BITCOUNT          1
#endif

typedef struct _DXGK_DIAGTYPE_NOTIFICATIONS
{
    union
    {
        struct
        {
            UINT PanelSelfRefreshSoftware : 1;
            UINT PanelSelfRefreshHardware : 1;
            UINT Reserved                 : 30;
        };
        UINT Value;
    };
} DXGK_DIAGTYPE_NOTIFICATIONS;

#define DXGK_DIAG_NOTIFICATIONS_PSR_SW_BIT   0
#define DXGK_DIAG_NOTIFICATIONS_PSR_SW_MASK  (1 << DXGK_DIAG_NOTIFICATIONS_PSR_SW_BIT)
#define DXGK_DIAG_NOTIFICATIONS_PSR_HW_BIT   1
#define DXGK_DIAG_NOTIFICATIONS_PSR_HW_MASK  (1 << DXGK_DIAG_NOTIFICATIONS_PSR_HW_BIT)
#define DXGK_DIAG_NOTIFICATIONS_BITCOUNT     2

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
typedef struct _DXGK_DIAGTYPE_PROGRESSIONS
{
    union
    {
        struct
        {
            UINT SyncLockEnableSync : 1;
            UINT Reserved           : 31;
        };
        UINT Value;
    };
} DXGK_DIAGTYPE_PROGRESSIONS;

#define DXGK_DIAG_PROGRESSIONS_SYNCLOCK_ENABLE_SYNC_BIT  0
#define DXGK_DIAG_PROGRESSIONS_SYNCLOCK_ENABLE_SYNC_MASK \
    (1 << DXGK_DIAG_PROGRESSIONS_SYNCLOCK_ENABLE_SYNC_BIT)
#define DXGK_DIAG_PROGRESSIONS_BITCOUNT                   1
#endif

typedef struct _DXGK_DIAGNOSTIC_TYPES
{
    union
    {
        DXGK_DIAGTYPE_NOTIFICATIONS Notifications;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
        DXGK_DIAGTYPE_PROGRESSIONS Progressions;
#endif
        UINT Value;
    };
} DXGK_DIAGNOSTIC_TYPES;

typedef struct _DXGK_DIAGNOSTIC_HEADER
{
    DXGK_DIAGNOSTIC_CATEGORIES Category;
    DXGK_DIAGNOSTIC_TYPES      Type;
    union
    {
        struct
        {
            UINT Size     : 16;
            UINT Reserved : 16;
        };
        UINT Value;
    };
    UINT SequenceNumber;
    union
    {
        D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
        D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
        UINT                           Id;
    };
} DXGK_DIAGNOSTIC_HEADER;

typedef union _DXGK_DIAGNOSTIC_PSR_REFRESH_REASON
{
    struct
    {
        UINT Present                   : 1;
        UINT CursorUpdate              : 1;
        UINT VSyncEnabled              : 1;
        UINT ColorTransformationChange : 1;
        UINT BrightnessChange          : 1;
        UINT SinkRequest               : 1;
        UINT Other                     : 1;
        UINT Reserved                  : 25;
    };
    UINT Value;
} DXGK_DIAGNOSTIC_PSR_REFRESH_REASON;

typedef struct _DXGK_DIAGNOSTIC_PSR
{
    DXGK_DIAGNOSTIC_HEADER Header;
    union
    {
        DXGK_DIAGNOSTIC_PSR_REFRESH_REASON RefreshReason;
        UINT                               Value;
    };
} DXGK_DIAGNOSTIC_PSR;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
typedef struct _DXGK_DIAGNOSTIC_SYNCLOCK_ENABLESYNC
{
    DXGK_DIAGNOSTIC_HEADER Header;
    union
    {
        struct
        {
            UINT DuringSetTiming : 1;
            UINT EnableSyncStart : 1;
            UINT EnableSyncEnd   : 1;
            UINT Reserved        : 29;
        } SyncLockEnableSync;
        UINT Value;
    };
} DXGK_DIAGNOSTIC_SYNCLOCK_ENABLESYNC;
#endif

typedef _In_ DXGK_DIAGNOSTIC_HEADER *IN_PDXGK_DIAGNOSTIC_HEADER;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_REPORT_DIAGNOSTIC)
    _IRQL_requires_max_(DISPATCH_LEVEL)
    _IRQL_requires_same_
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_REPORT_DIAGNOSTIC)(
    _In_ HANDLE DeviceHandle,
    IN_PDXGK_DIAGNOSTIC_HEADER pDiagnostic);

C_ASSERT(sizeof(DXGK_DIAGNOSTIC_CATEGORIES) == 0x4);
C_ASSERT(sizeof(DXGK_DIAGTYPE_NOTIFICATIONS) == 0x4);
C_ASSERT(sizeof(DXGK_DIAGNOSTIC_TYPES) == 0x4);
C_ASSERT(sizeof(DXGK_DIAGNOSTIC_HEADER) == 0x14);
C_ASSERT(FIELD_OFFSET(DXGK_DIAGNOSTIC_HEADER, Type) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGK_DIAGNOSTIC_HEADER, SequenceNumber) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGK_DIAGNOSTIC_HEADER, Id) == 0x10);
C_ASSERT(sizeof(DXGK_DIAGNOSTIC_PSR_REFRESH_REASON) == 0x4);
C_ASSERT(sizeof(DXGK_DIAGNOSTIC_PSR) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGK_DIAGNOSTIC_PSR, RefreshReason) == 0x14);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
C_ASSERT(sizeof(DXGK_DIAGTYPE_PROGRESSIONS) == 0x4);
C_ASSERT(sizeof(DXGK_DIAGNOSTIC_SYNCLOCK_ENABLESYNC) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGK_DIAGNOSTIC_SYNCLOCK_ENABLESYNC, Value) == 0x14);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4 */

/* =========================================================================
 * DXGK_INTERFACE  (officially DXGKRNL_INTERFACE in Windows WDK docs)
 *
 * Vtable of dxgkrnl service callbacks handed to the miniport at
 * DxgkDdiStartDevice time.  The miniport saves this structure for later use.
 *
 * The Size and Version fields allow version negotiation; dxgkrnl will
 * not populate fields beyond the structure size declared by the miniport.
 *
 * Field order verified against the official Microsoft documentation:
 *   https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/
 *       dispmprt/ns-dispmprt-_dxgkrnl_interface
 *
 * WDDM 1.0 (Vista) baseline fields — offsets on amd64:
 *   0x00: Size (ULONG)
 *   0x04: Version (ULONG)
 *   0x08: DeviceHandle (HANDLE)
 *   0x10: DxgkCbEvalAcpiMethod
 *   0x18: DxgkCbGetDeviceInformation
 *   0x20: DxgkCbIndicateChildStatus
 *   0x28: DxgkCbMapMemory
 *   0x30: DxgkCbQueueDpc
 *   0x38: DxgkCbQueryServices
 *   0x40: DxgkCbReadDeviceSpace
 *   0x48: DxgkCbSynchronizeExecution
 *   0x50: DxgkCbUnmapMemory
 *   0x58: DxgkCbWriteDeviceSpace
 *   0x60: DxgkCbIsDevicePresent
 *   0x68: DxgkCbGetHandleData
 *   0x70: DxgkCbGetHandleParent
 *   0x78: DxgkCbEnumHandleChildren
 *   0x80: DxgkCbNotifyInterrupt
 *   0x88: DxgkCbNotifyDpc
 *   0x90: DxgkCbQueryVidPnInterface
 *   0x98: DxgkCbQueryMonitorInterface
 *   0xa0: DxgkCbGetCaptureAddress
 *   0xa8: DxgkCbLogEtwEvent            (Vista SP1)
 *   0xb0: DxgkCbExcludeAdapterAccess   (Vista SP1 / Win7)
 *   0xb8+: Win8 and later additions
 * =========================================================================
 */
typedef struct _DXGK_INTERFACE
{
    ULONG   Size;                                       /* 0x00 */
    ULONG   Version;                                    /* 0x04 */
    HANDLE  DeviceHandle;                               /* 0x08 */

    /* --- WDDM 1.0 (Vista) baseline callbacks --- */
    DXGKCB_EVAL_ACPI_METHOD DxgkCbEvalAcpiMethod;       /* 0x10 */
    DXGKCB_GET_DEVICE_INFORMATION DxgkCbGetDeviceInformation; /* 0x18 */
    DXGKCB_INDICATE_CHILD_STATUS DxgkCbIndicateChildStatus;   /* 0x20 */
    DXGKCB_MAP_MEMORY DxgkCbMapMemory;                  /* 0x28 */
    DXGKCB_QUEUE_DPC DxgkCbQueueDpc;                    /* 0x30 */
    DXGKCB_QUERY_SERVICES DxgkCbQueryServices;          /* 0x38 */
    DXGKCB_READ_DEVICE_SPACE DxgkCbReadDeviceSpace;     /* 0x40 */
    DXGKCB_SYNCHRONIZE_EXECUTION DxgkCbSynchronizeExecution; /* 0x48 */
    DXGKCB_UNMAP_MEMORY DxgkCbUnmapMemory;              /* 0x50 */
    DXGKCB_WRITE_DEVICE_SPACE DxgkCbWriteDeviceSpace;   /* 0x58 */
    DXGKCB_IS_DEVICE_PRESENT DxgkCbIsDevicePresent;     /* 0x60 */
    DXGKCB_GETHANDLEDATA DxgkCbGetHandleData;           /* 0x68 */
    DXGKCB_GETHANDLEPARENT DxgkCbGetHandleParent;       /* 0x70 */
    DXGKCB_ENUMHANDLECHILDREN DxgkCbEnumHandleChildren; /* 0x78 */
    DXGKCB_NOTIFY_INTERRUPT   DxgkCbNotifyInterrupt;    /* 0x80 */
    DXGKCB_NOTIFY_DPC         DxgkCbNotifyDpc;          /* 0x88 */
    DXGKCB_QUERYVIDPNINTERFACE     DxgkCbQueryVidPnInterface;    /* 0x90 */
    DXGKCB_QUERYMONITORINTERFACE   DxgkCbQueryMonitorInterface;  /* 0x98 */
    DXGKCB_GETCAPTUREADDRESS DxgkCbGetCaptureAddress;   /* 0xa0 */

    /* --- Vista SP1 / Win7 additions --- */
    DXGKCB_LOG_ETW_EVENT DxgkCbLogEtwEvent;             /* 0xa8 */
    DXGKCB_EXCLUDE_ADAPTER_ACCESS DxgkCbExcludeAdapterAccess; /* 0xb0 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    /* --- Win8 (WDDM 1.2) additions --- */
    DXGKCB_CREATECONTEXTALLOCATION DxgkCbCreateContextAllocation; /* 0xb8 */
    DXGKCB_DESTROYCONTEXTALLOCATION DxgkCbDestroyContextAllocation; /* 0xc0 */
    DXGKCB_SETPOWERCOMPONENTACTIVE DxgkCbSetPowerComponentActive; /* 0xc8 */
    DXGKCB_SETPOWERCOMPONENTIDLE DxgkCbSetPowerComponentIdle; /* 0xd0 */
    DXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP DxgkCbAcquirePostDisplayOwnership; /* 0xd8 */
    DXGKCB_POWERRUNTIMECONTROLREQUEST DxgkCbPowerRuntimeControlRequest; /* 0xe0 */
    DXGKCB_SETPOWERCOMPONENTLATENCY DxgkCbSetPowerComponentLatency; /* 0xe8 */
    DXGKCB_SETPOWERCOMPONENTRESIDENCY DxgkCbSetPowerComponentResidency; /* 0xf0 */
    DXGKCB_COMPLETEFSTATETRANSITION DxgkCbCompleteFStateTransition; /* 0xf8 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    /* --- Win8.1 (WDDM 1.3) --- */
    DXGKCB_COMPLETEPSTATETRANSITION DxgkCbCompletePStateTransition; /* 0x100 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    /* --- WDDM 2.0 additions --- */
    DXGKCB_MAPCONTEXTALLOCATION DxgkCbMapContextAllocation; /* 0x108 */
    DXGKCB_UPDATECONTEXTALLOCATION DxgkCbUpdateContextAllocation; /* 0x110 */
    DXGKCB_RESERVEGPUVIRTUALADDRESSRANGE DxgkCbReserveGpuVirtualAddressRange; /* 0x118 */
    DXGKCB_ACQUIREHANDLEDATA DxgkCbAcquireHandleData;   /* 0x120 */
    DXGKCB_RELEASEHANDLEDATA DxgkCbReleaseHandleData;   /* 0x128 */
    DXGKCB_HARDWARECONTENTPROTECTIONTEARDOWN DxgkCbHardwareContentProtectionTeardown; /* 0x130 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    /* --- WDDM 2.1 additions --- */
    DXGKCB_MULTIPLANEOVERLAYDISABLED DxgkCbMultiPlaneOverlayDisabled; /* 0x138 */
    DXGKCB_DXGKCB_MITIGATEDRANGEUPDATE DxgkCbMitigatedRangeUpdate; /* 0x140 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    /* --- WDDM 2.2 additions --- */
    DXGKCB_INVALIDATEHWCONTEXT DxgkCbInvalidateHwContext; /* 0x148 */
    DXGKCB_INDICATE_CONNECTOR_CHANGE DxgkCbIndicateConnectorChange; /* 0x150 */
    DXGKCB_UNBLOCKUEFIFRAMEBUFFERRANGES DxgkCbUnblockUEFIFrameBufferRanges; /* 0x158 */
    DXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP2 DxgkCbAcquirePostDisplayOwnership2; /* 0x160 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    /* --- WDDM 2.3 additions --- */
    DXGKCB_SETPROTECTEDSESSIONSTATUS DxgkCbSetProtectedSessionStatus; /* 0x168 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    /* --- WDDM 2.4 additions --- */
    DXGKCB_ALLOCATECONTIGUOUSMEMORY DxgkCbAllocateContiguousMemory; /* 0x170 */
    DXGKCB_FREECONTIGUOUSMEMORY DxgkCbFreeContiguousMemory;       /* 0x178 */
    DXGKCB_ALLOCATEPAGESFORMDL DxgkCbAllocatePagesForMdl;         /* 0x180 */
    DXGKCB_FREEPAGESFROMMDL DxgkCbFreePagesFromMdl;                /* 0x188 */
    DXGKCB_PINFRAMEBUFFERFORSAVE DxgkCbPinFrameBufferForSave;      /* 0x190 */
    DXGKCB_UNPINFRAMEBUFFERFORSAVE DxgkCbUnpinFrameBufferForSave;  /* 0x198 */
    DXGKCB_MAPFRAMEBUFFERPOINTER DxgkCbMapFrameBufferPointer;      /* 0x1a0 */
    DXGKCB_UNMAPFRAMEBUFFERPOINTER DxgkCbUnmapFrameBufferPointer;  /* 0x1a8 */
    DXGKCB_MAPMDLTOIOMMU DxgkCbMapMdlToIoMmu;                     /* 0x1b0 */
    DXGKCB_UNMAPMDLFROMIOMMU DxgkCbUnmapMdlFromIoMmu;             /* 0x1b8 */
    DXGKCB_REPORT_DIAGNOSTIC DxgkCbReportDiagnostic;    /* 0x1c0 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    /* --- WDDM 2.5+ additions --- */
    DXGKCB_SIGNALEVENT DxgkCbSignalEvent;               /* 0x1c8 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
    /* --- WDDM 2.6 additions --- */
    DXGKCB_ISFEATUREENABLED DxgkCbIsFeatureEnabled;     /* 0x1d0 */
    DXGKCB_SAVEMEMORYFORHOTUPDATE DxgkCbSaveMemoryForHotUpdate; /* 0x1d8 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)
    /* --- WDDM 2.8 --- */
    DXGKCB_NOTIFYCURSORSUPPORTCHANGE DxgkCbNotifyCursorSupportChange; /* 0x1e0 */
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    /* --- WDDM 2.9 additions --- */
    DXGKCB_QUERYFEATURESUPPORT          DxgkCbQueryFeatureSupport;
    DXGKCB_CREATEPHYSICALMEMORYOBJECT   DxgkCbCreatePhysicalMemoryObject;
    DXGKCB_DESTROYPHYSICALMEMORYOBJECT  DxgkCbDestroyPhysicalMemoryObject;
    DXGKCB_MAPPHYSICALMEMORY            DxgkCbMapPhysicalMemory;
    DXGKCB_UNMAPPHYSICALMEMORY          DxgkCbUnmapPhysicalMemory;
    DXGKCB_ALLOCATEADL                  DxgkCbAllocateAdl;
    DXGKCB_FREEADL                      DxgkCbFreeAdl;
    DXGKCB_OPENPHYSICALMEMORYOBJECT     DxgkCbOpenPhysicalMemoryObject;
    DXGKCB_CLOSEPHYSICALMEMORYOBJECT    DxgkCbClosePhysicalMemoryObject;
    DXGKCB_PINFRAMEBUFFERFORSAVE2       DxgkCbPinFrameBufferForSave2;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
    /* --- WDDM 3.1 --- */
    DXGKCB_DISCONNECTDOORBELL DxgkCbDisconnectDoorbell; /* 0x238 */
#endif
} DXGK_INTERFACE, *PDXGK_INTERFACE;

/* Windows WDK uses DXGKRNL_INTERFACE as the official name */
typedef DXGK_INTERFACE  DXGKRNL_INTERFACE;
typedef DXGK_INTERFACE *PDXGKRNL_INTERFACE;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_4)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbSetProtectedSessionStatus) == 0x168);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x170);
#else
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbSetProtectedSessionStatus) == 0xB8);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0xBC);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_5)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAllocateContiguousMemory) == 0x170);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbReportDiagnostic) == 0x1C0);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x1C8);
#else
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAllocateContiguousMemory) == 0xBC);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbReportDiagnostic) == 0xE4);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0xE8);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_6)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbSignalEvent) == 0x1C8);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x1D0);
#else
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbSignalEvent) == 0xE8);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0xEC);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_8)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbIsFeatureEnabled) == 0x1D0);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbSaveMemoryForHotUpdate) == 0x1D8);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x1E0);
#else
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbIsFeatureEnabled) == 0xEC);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbSaveMemoryForHotUpdate) == 0xF0);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0xF4);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_9)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbNotifyCursorSupportChange) == 0x1E0);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x1E8);
#else
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbNotifyCursorSupportChange) == 0xF4);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0xF8);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM3_0)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbQueryFeatureSupport) == 0x1E8);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbCreatePhysicalMemoryObject) == 0x1F0);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbDestroyPhysicalMemoryObject) == 0x1F8);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbMapPhysicalMemory) == 0x200);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbUnmapPhysicalMemory) == 0x208);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAllocateAdl) == 0x210);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbFreeAdl) == 0x218);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbOpenPhysicalMemoryObject) == 0x220);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbClosePhysicalMemoryObject) == 0x228);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbPinFrameBufferForSave2) == 0x230);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x238);
#else
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbQueryFeatureSupport) == 0xF8);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbCreatePhysicalMemoryObject) == 0xFC);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbDestroyPhysicalMemoryObject) == 0x100);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbMapPhysicalMemory) == 0x104);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbUnmapPhysicalMemory) == 0x108);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAllocateAdl) == 0x10C);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbFreeAdl) == 0x110);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbOpenPhysicalMemoryObject) == 0x114);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbClosePhysicalMemoryObject) == 0x118);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbPinFrameBufferForSave2) == 0x11C);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x120);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM3_1)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbQueryFeatureSupport) == 0x1E8);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbPinFrameBufferForSave2) == 0x230);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x238);
#else
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbQueryFeatureSupport) == 0xF8);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbPinFrameBufferForSave2) == 0x11C);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x120);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbDisconnectDoorbell) == 0x238);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x240);
#else
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbDisconnectDoorbell) == 0x120);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 0x124);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
/* Forward-declare KMDDOD_INITIALIZATION_DATA — defined after DRIVER_INITIALIZATION_DATA */
typedef struct _KMDDOD_INITIALIZATION_DATA  KMDDOD_INITIALIZATION_DATA;
typedef struct _KMDDOD_INITIALIZATION_DATA *PKMDDOD_INITIALIZATION_DATA;

/* Public Displib API. Native miniports resolve its private dxgkrnl target at
 * runtime; ReactOS also retains a direct dxgkrnl compatibility export. */
NTSTATUS
APIENTRY
DxgkInitializeDisplayOnlyDriver(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PKMDDOD_INITIALIZATION_DATA KmDodInitializationData);
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
NTSTATUS
APIENTRY
DxgkUnInitialize(
    _In_ PDRIVER_OBJECT DriverObject);
#endif


/* =========================================================================
 * DXGKDDI_* miniport callback typedefs
 *
 * One typedef per DDI entry point.  The miniport fills a
 * DRIVER_INITIALIZATION_DATA with function pointers of these types and
 * passes it to DxgkInitialize().
 *
 * All callbacks use APIENTRY (__stdcall on x86, direct call on x64).
 * =========================================================================
 */

/* ---- PnP / power lifecycle -------------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_ADD_DEVICE)(
    _In_  PDEVICE_OBJECT    PhysicalDeviceObject,
    _Out_ PVOID            *MiniportDeviceContext);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_START_DEVICE)(
    _In_  PVOID             MiniportDeviceContext,
    _In_  PDXGK_START_INFO  DxgkStartInfo,
    _In_  PDXGK_INTERFACE   DxgkInterface,
    _Out_ PULONG            NumberOfVideoPresentSources,
    _Out_ PULONG            NumberOfChildren);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_STOP_DEVICE)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_REMOVE_DEVICE)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_DISPATCH_IO_REQUEST)(
    _In_ PVOID                  MiniportDeviceContext,
    _In_ ULONG                  VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET  VideoRequestPacket);

typedef BOOLEAN
(APIENTRY *PDXGKDDI_INTERRUPT_ROUTINE)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ ULONG  MessageNumber);

typedef VOID
(APIENTRY *PDXGKDDI_DPC_ROUTINE)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_QUERY_CHILD_RELATIONS)(
    _In_  PVOID                     MiniportDeviceContext,
    _Out_ PDXGK_CHILD_DESCRIPTOR    ChildRelations,
    _In_  ULONG                     ChildRelationsSize);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_QUERY_CHILD_STATUS)(
    _In_    PVOID               MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS  ChildStatus,
    _In_    BOOLEAN             NonDestructiveOnly);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_QUERY_DEVICE_DESCRIPTOR)(
    _In_    PVOID                       MiniportDeviceContext,
    _In_    ULONG                       ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR     DeviceDescriptor);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SET_POWER_STATE)(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_NOTIFY_ACPI_EVENT)(
    _In_      PVOID             MiniportDeviceContext,
    _In_      DXGK_EVENT_TYPE   EventType,
    _In_      ULONG             Event,
    _In_      PVOID             Argument,
    _Out_opt_ PULONG            AcpiFlags);

typedef VOID
(APIENTRY *PDXGKDDI_RESET_DEVICE)(
    _In_ PVOID MiniportDeviceContext);

typedef VOID
(APIENTRY *PDXGKDDI_UNLOAD)(VOID);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_QUERY_INTERFACE)(
    _In_ PVOID              MiniportDeviceContext,
    _In_ PQUERY_INTERFACE   QueryInterface);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CONTROL_ETW_LOGGING)(
    _In_ BOOLEAN    Enable,
    _In_ ULONG      Flags,
    _In_ UCHAR      Level);

/* ---- Adapter information / capabilities -------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_QUERY_ADAPTER_INFO)(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO    *QueryAdapterInfo);

/* ---- Device / allocation management ------------------------------------ */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CREATE_DEVICE)(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEDEVICE   CreateDevice);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CREATE_ALLOCATION)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEALLOCATION   CreateAllocation);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_DESTROY_ALLOCATION)(
    _In_ PVOID                           MiniportDeviceContext,
    _In_ CONST DXGKARG_DESTROYALLOCATION *DestroyAllocation);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_DESCRIBE_ALLOCATION)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_DESCRIBEALLOCATION DescribeAllocation);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_GET_STDALLOC_UPDATEFLAGS)(
    _In_    PVOID  MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA StandardAllocationDriverData);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_ACQUIRE_SWIZZLING_RANGE)(
    _In_    PVOID                           MiniportDeviceContext,
    _Inout_ PDXGKARG_ACQUIRESWIZZLINGRANGE  AcquireSwizzlingRange);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_RELEASE_SWIZZLING_RANGE)(
    _In_ PVOID                          MiniportDeviceContext,
    _In_ PDXGKARG_RELEASESWIZZLINGRANGE ReleaseSwizzlingRange);

/* ---- DMA command buffer submission ------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_PATCH)(
    _In_ PVOID                 MiniportDeviceContext,
    _In_ CONST DXGKARG_PATCH  *Patch);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SUBMIT_COMMAND)(
    _In_ PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_PREEMPT_COMMAND)(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *PreemptCommand);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_BUILD_PAGING_BUFFER)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER  BuildPagingBuffer);

/* ---- Palette / pointer / present --------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SET_PALETTE)(
    _In_ PVOID                   MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPALETTE *SetPalette);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SET_POINTER_POSITION)(
    _In_ PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SET_POINTER_SHAPE)(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape);

/* ---- TDR (Timeout Detection and Recovery) ------------------------------ */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_RESETFROMTIMEOUT)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_RESTARTFROMTIMEOUT)(
    _In_ PVOID MiniportDeviceContext);

/* ---- Escape / debug ---------------------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_ESCAPE)(
    _In_ PVOID                  MiniportDeviceContext,
    _In_ CONST DXGKARG_ESCAPE  *Escape);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_COLLECT_DB_ENGINE_INFO)(
    _In_  PVOID                               MiniportDeviceContext,
    _In_  CONST DXGKARG_COLLECTDBGINFO       *CollectDbEngineInfo);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_QUERY_CURRENT_FENCE)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYCURRENTFENCE  CurrentFence);

/* ---- VidPN management -------------------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_IS_SUPPORTED_VIDPN)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_ISSUPPORTEDVIDPN   IsSupportedVidPn);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN)(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *RecommendFunctionalVidPn);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_ENUM_VIDPN_COFUNC_MODALITY)(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *EnumCofuncModality);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SET_VIDPN_SOURCE_ADDRESS)(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SET_VIDPN_SOURCE_VISIBILITY)(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_COMMIT_VIDPN)(
    _In_ PVOID                      MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN *CommitVidPn);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH)(
    _In_ PVOID                                      MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_RECOMMEND_MONITORMODES)(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *RecommendMonitorModes);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_RECOMMEND_VIDPN_TOPOLOGY)(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *RecommendVidPnTopology);

/* ---- Scan-line / interrupt control ------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_GET_SCAN_LINE)(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSCANLINE    GetScanLine);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_STOP_CAPTURE)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ PVOID  StopCapture);       /* PDXGKARG_STOPCAPTURE placeholder */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CONTROL_INTERRUPT)(
    _In_ PVOID                      MiniportDeviceContext,
    _In_ CONST DXGK_INTERRUPT_TYPE  InterruptType,
    _In_ BOOLEAN                    EnableInterrupt);

/* ---- Overlay ----------------------------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CREATE_OVERLAY)(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEOVERLAY  CreateOverlay);

/* ---- Per-device/context/allocation callbacks (device-level DDIs) ------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_DESTROY_DEVICE)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_OPEN_ALLOCATION)(
    _In_ PVOID                           MiniportDeviceContext,
    _In_ CONST DXGKARG_OPENALLOCATION   *OpenAllocation);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CLOSE_ALLOCATION)(
    _In_ PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_CLOSEALLOCATION   *CloseAllocation);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_RENDER)(
    _In_    PVOID           MiniportDeviceContext,
    _Inout_ PDXGKARG_RENDER Render);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_PRESENT)(
    _In_    PVOID               MiniportDeviceContext,
    _Inout_ PDXGKARG_PRESENT    Present);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_UPDATE_OVERLAY)(
    _In_ PVOID                       MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEOVERLAY *UpdateOverlay);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_FLIP_OVERLAY)(
    _In_ PVOID                     MiniportDeviceContext,
    _In_ CONST DXGKARG_FLIPOVERLAY *FlipOverlay);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_DESTROY_OVERLAY)(
    _In_ PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_DESTROYOVERLAY *DestroyOverlay);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CREATE_CONTEXT)(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATECONTEXT  CreateContext);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_DESTROY_CONTEXT)(
    _In_ PVOID MiniportDeviceContext);

/* ---- Multi-GPU linked adapter ----------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_LINK_DEVICE)(
    _In_    CONST PDEVICE_OBJECT    PhysicalDeviceObject,
    _In_    CONST PVOID             MiniportDeviceContext,
    _Inout_ PLINKED_DEVICE          LinkedDevice);

/* ---- Private display driver format ------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SET_DISPLAY_PRIVATE_DRIVER_FORMAT)(
    _In_ PVOID                                       MiniportDeviceContext,
    _In_ CONST DXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT *SetDisplayPrivateDriverFormat);

/* ---- WDDM 1.1 (Win7) per-engine TDR callbacks ------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_QUERY_ENGINE_STATUS)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYENGINESTATUS  QueryEngineStatus);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_RESET_ENGINE)(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_RESETENGINE    ResetEngine);

/* ---- WDDM 1.1 (Win7) VidPN hardware capabilities --------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKDDI_QUERY_VIDPN_HW_CAPABILITY)(
    _In_    PVOID                           MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYVIDPNHWCAPABILITY QueryVidPnHwCapability);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP)(
    _In_ PVOID                           MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID  TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION      DisplayInfo);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SYSTEM_DISPLAY_ENABLE)(
    _In_  PVOID                              MiniportDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID     TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ PUINT                              Width,
    _Out_ PUINT                              Height,
    _Out_ D3DDDIFORMAT                      *ColorFormat);

typedef VOID
(APIENTRY *PDXGKDDI_SYSTEM_DISPLAY_WRITE)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ PVOID  Source,
    _In_ UINT   SourceWidth,
    _In_ UINT   SourceHeight,
    _In_ UINT   SourceStride,
    _In_ UINT   PositionX,
    _In_ UINT   PositionY);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CANCEL_COMMAND)(
    _In_ PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_CANCELCOMMAND *CancelCommand);

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
typedef NTSTATUS
DXGKDDI_NOTIFY_SURPRISE_REMOVAL(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGK_SURPRISE_REMOVAL_TYPE RemovalType);

typedef DXGKDDI_NOTIFY_SURPRISE_REMOVAL *PDXGKDDI_NOTIFY_SURPRISE_REMOVAL;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
typedef NTSTATUS
(APIENTRY *PDXGKDDI_GET_NODE_METADATA)(
    _In_  PVOID                      MiniportDeviceContext,
    _In_  UINT                       NodeOrdinal,
    _Out_ DXGKARG_GETNODEMETADATA   *GetNodeMetadata);
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)

typedef struct _DXGKARG_QUERYDIAGNOSTICTYPESSUPPORT
{
    _In_  DXGK_DIAGNOSTIC_CATEGORIES DiagnosticCategory;
    _Out_ DXGK_DIAGNOSTIC_TYPES      NoninvasiveTypes;
    _Out_ DXGK_DIAGNOSTIC_TYPES      InvasiveTypes;
} DXGKARG_QUERYDIAGNOSTICTYPESSUPPORT,
 *PDXGKARG_QUERYDIAGNOSTICTYPESSUPPORT;

typedef _Inout_ PDXGKARG_QUERYDIAGNOSTICTYPESSUPPORT
    INOUT_PDXGKARG_QUERYDIAGNOSTICTYPESSUPPORT;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_QUERYDIAGNOSTICTYPESSUPPORT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_QUERYDIAGNOSTICTYPESSUPPORT(
    IN_CONST_PVOID MiniportDeviceContext,
    INOUT_PDXGKARG_QUERYDIAGNOSTICTYPESSUPPORT
        pArgQueryDiagnosticTypesSupport);

typedef struct _DXGKARG_CONTROLDIAGNOSTICREPORTING
{
    _In_ DXGK_DIAGNOSTIC_CATEGORIES DiagnosticCategory;
    _In_ DXGK_DIAGNOSTIC_TYPES      RequestedDiagnostics;
} DXGKARG_CONTROLDIAGNOSTICREPORTING,
 *PDXGKARG_CONTROLDIAGNOSTICREPORTING;

typedef _In_ PDXGKARG_CONTROLDIAGNOSTICREPORTING
    IN_PDXGKARG_CONTROLDIAGNOSTICREPORTING;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CONTROLDIAGNOSTICREPORTING)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_CONTROLDIAGNOSTICREPORTING(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_PDXGKARG_CONTROLDIAGNOSTICREPORTING
        pArgControlDiagnosticReporting);

typedef DXGKDDI_QUERYDIAGNOSTICTYPESSUPPORT
    *PDXGKDDI_QUERYDIAGNOSTICTYPESSUPPORT;
typedef DXGKDDI_CONTROLDIAGNOSTICREPORTING
    *PDXGKDDI_CONTROLDIAGNOSTICREPORTING;

C_ASSERT(sizeof(DXGKARG_QUERYDIAGNOSTICTYPESSUPPORT) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_QUERYDIAGNOSTICTYPESSUPPORT, NoninvasiveTypes) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_QUERYDIAGNOSTICTYPESSUPPORT, InvasiveTypes) == 0x8);
C_ASSERT(sizeof(DXGKARG_CONTROLDIAGNOSTICREPORTING) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_CONTROLDIAGNOSTICREPORTING, RequestedDiagnostics) == 0x4);

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)

typedef struct _DXGKARG_SETTARGETADJUSTEDCOLORIMETRY2
{
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    _In_ DXGK_COLORIMETRY               AdjustedColorimetry;
    _In_ UINT                           SdrWhiteLevel;
} DXGKARG_SETTARGETADJUSTEDCOLORIMETRY2,
 *PDXGKARG_SETTARGETADJUSTEDCOLORIMETRY2;

typedef _In_ PDXGKARG_SETTARGETADJUSTEDCOLORIMETRY2
    IN_PDXGKARG_SETTARGETADJUSTEDCOLORIMETRY2;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY2)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY2(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_SETTARGETADJUSTEDCOLORIMETRY2
        pArgSetTargetAdjustedColorimetry);

typedef DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY2
    *PDXGKDDI_SETTARGETADJUSTEDCOLORIMETRY2;

C_ASSERT(sizeof(DXGKARG_SETTARGETADJUSTEDCOLORIMETRY2) == 0x3C);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETTARGETADJUSTEDCOLORIMETRY2, AdjustedColorimetry) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETTARGETADJUSTEDCOLORIMETRY2, SdrWhiteLevel) == 0x38);

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)

#define DXGK_DUMP_BUCKETING_BUFFER_SIZE 64
#define DXGK_DUMP_DESCRIPTION_BUFFER_SIZE 128

typedef enum _DXGK_DIAGNOSTICINFO_TYPE
{
    DXGK_DI_ADDDEVICE = 1,
    DXGK_DI_STARTDEVICE,
    DXGK_DI_BLACKSCREEN
} DXGK_DIAGNOSTICINFO_TYPE;

typedef struct _DXGKARG_COLLECTDIAGNOSTICINFO
{
    HANDLE                   hAdapter;
    DXGK_DIAGNOSTICINFO_TYPE Type;
    CHAR                     BucketingString[DXGK_DUMP_BUCKETING_BUFFER_SIZE];
    CHAR                     DescriptionString[DXGK_DUMP_DESCRIPTION_BUFFER_SIZE];
    union
    {
        PVOID pReserved;
    };
    UINT                     BufferSizeIn;
    UINT                     BufferSizeOut;
    PVOID                    pBuffer;
} DXGKARG_COLLECTDIAGNOSTICINFO;

typedef _Inout_ DXGKARG_COLLECTDIAGNOSTICINFO
    *INOUT_PDXGKARG_COLLECTDIAGNOSTICINFO;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_COLLECTDIAGNOSTICINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_COLLECTDIAGNOSTICINFO(
    IN_CONST_PDEVICE_OBJECT PhysicalDeviceObject,
    INOUT_PDXGKARG_COLLECTDIAGNOSTICINFO pCollectDiagnosticInfo);

typedef DXGKDDI_COLLECTDIAGNOSTICINFO
    *PDXGKDDI_COLLECTDIAGNOSTICINFO;

C_ASSERT(sizeof(DXGK_DIAGNOSTICINFO_TYPE) == 0x4);
#ifdef _WIN64
C_ASSERT(sizeof(DXGKARG_COLLECTDIAGNOSTICINFO) == 0xE8);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, Type) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, BucketingString) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, DescriptionString) == 0x4C);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, pReserved) == 0xD0);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, BufferSizeIn) == 0xD8);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, BufferSizeOut) == 0xDC);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, pBuffer) == 0xE0);
#else
C_ASSERT(sizeof(DXGKARG_COLLECTDIAGNOSTICINFO) == 0xD8);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, Type) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, BucketingString) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, DescriptionString) == 0x48);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, pReserved) == 0xC8);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, BufferSizeIn) == 0xCC);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, BufferSizeOut) == 0xD0);
C_ASSERT(FIELD_OFFSET(DXGKARG_COLLECTDIAGNOSTICINFO, pBuffer) == 0xD4);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6 */


/* =========================================================================
 * DRIVER_INITIALIZATION_DATA
 *
 * The central DDI registration structure.  The miniport allocates this on
 * the stack or in a global, fills in the Version field and all mandatory
 * callbacks, and passes a pointer to DxgkInitialize().
 *
 * Version MUST be DXGKDDI_INTERFACE_VERSION_VISTA or higher.
 *
 * The base (WDDM 1.0 / Vista) layout contains 62 slots (1 ULONG Version
 * + 61 function pointers) for a total of 496 bytes on amd64.  Successive
 * WDDM versions add fields at the end, guarded by #if on
 * DXGKDDI_INTERFACE_VERSION.  dxgkrnl accepts larger or smaller
 * structures; it copies min(caller size, our size) bytes.
 * =========================================================================
 */
typedef struct _DRIVER_INITIALIZATION_DATA
{
    /*
     * Must be set to DXGKDDI_INTERFACE_VERSION_VISTA or higher before
     * calling DxgkInitialize.  dxgkrnl rejects lower values.
     */
    ULONG   Version;

    /* ---- PnP / power lifecycle ---------------------------------------- */
    PDXGKDDI_ADD_DEVICE                         DxgkDdiAddDevice;
    PDXGKDDI_START_DEVICE                       DxgkDdiStartDevice;
    PDXGKDDI_STOP_DEVICE                        DxgkDdiStopDevice;
    PDXGKDDI_REMOVE_DEVICE                      DxgkDdiRemoveDevice;
    PDXGKDDI_DISPATCH_IO_REQUEST                DxgkDdiDispatchIoRequest;
    PDXGKDDI_INTERRUPT_ROUTINE                  DxgkDdiInterruptRoutine;
    PDXGKDDI_DPC_ROUTINE                        DxgkDdiDpcRoutine;
    PDXGKDDI_QUERY_CHILD_RELATIONS              DxgkDdiQueryChildRelations;
    PDXGKDDI_QUERY_CHILD_STATUS                 DxgkDdiQueryChildStatus;
    PDXGKDDI_QUERY_DEVICE_DESCRIPTOR            DxgkDdiQueryDeviceDescriptor;
    PDXGKDDI_SET_POWER_STATE                    DxgkDdiSetPowerState;
    PDXGKDDI_NOTIFY_ACPI_EVENT                  DxgkDdiNotifyAcpiEvent;
    PDXGKDDI_RESET_DEVICE                       DxgkDdiResetDevice;
    PDXGKDDI_UNLOAD                             DxgkDdiUnload;
    PDXGKDDI_QUERY_INTERFACE                    DxgkDdiQueryInterface;
    PDXGKDDI_CONTROL_ETW_LOGGING                DxgkDdiControlEtwLogging;

    /* ---- Adapter information / capabilities ----------------------------- */
    PDXGKDDI_QUERY_ADAPTER_INFO                 DxgkDdiQueryAdapterInfo;

    /* ---- Device / allocation ------------------------------------------- */
    PDXGKDDI_CREATE_DEVICE                      DxgkDdiCreateDevice;
    PDXGKDDI_CREATE_ALLOCATION                  DxgkDdiCreateAllocation;
    PDXGKDDI_DESTROY_ALLOCATION                 DxgkDdiDestroyAllocation;
    PDXGKDDI_DESCRIBE_ALLOCATION                DxgkDdiDescribeAllocation;
    PDXGKDDI_GET_STDALLOC_UPDATEFLAGS           DxgkDdiGetStandardAllocationDriverData;
    PDXGKDDI_ACQUIRE_SWIZZLING_RANGE            DxgkDdiAcquireSwizzlingRange;
    PDXGKDDI_RELEASE_SWIZZLING_RANGE            DxgkDdiReleaseSwizzlingRange;

    /* ---- DMA command buffer submission ---------------------------------- */
    PDXGKDDI_PATCH                              DxgkDdiPatch;
    PDXGKDDI_SUBMIT_COMMAND                     DxgkDdiSubmitCommand;
    PDXGKDDI_PREEMPT_COMMAND                    DxgkDdiPreemptCommand;
    PDXGKDDI_BUILD_PAGING_BUFFER                DxgkDdiBuildPagingBuffer;

    /* ---- Palette / pointer --------------------------------------------- */
    PDXGKDDI_SET_PALETTE                        DxgkDdiSetPalette;
    PDXGKDDI_SET_POINTER_POSITION               DxgkDdiSetPointerPosition;
    PDXGKDDI_SET_POINTER_SHAPE                  DxgkDdiSetPointerShape;

    /* ---- TDR ----------------------------------------------------------- */
    PDXGKDDI_RESETFROMTIMEOUT                   DxgkDdiResetFromTimeout;
    PDXGKDDI_RESTARTFROMTIMEOUT                 DxgkDdiRestartFromTimeout;

    /* ---- Escape / debug ------------------------------------------------ */
    PDXGKDDI_ESCAPE                             DxgkDdiEscape;
    PDXGKDDI_COLLECT_DB_ENGINE_INFO             DxgkDdiCollectDbgInfo;
    PDXGKDDI_QUERY_CURRENT_FENCE                DxgkDdiQueryCurrentFence;

    /* ---- VidPN management ---------------------------------------------- */
    PDXGKDDI_IS_SUPPORTED_VIDPN                 DxgkDdiIsSupportedVidPn;
    PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN         DxgkDdiRecommendFunctionalVidPn;
    PDXGKDDI_ENUM_VIDPN_COFUNC_MODALITY         DxgkDdiEnumVidPnCofuncModality;
    PDXGKDDI_SET_VIDPN_SOURCE_ADDRESS           DxgkDdiSetVidPnSourceAddress;
    PDXGKDDI_SET_VIDPN_SOURCE_VISIBILITY        DxgkDdiSetVidPnSourceVisibility;
    PDXGKDDI_COMMIT_VIDPN                       DxgkDdiCommitVidPn;
    PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH   DxgkDdiUpdateActiveVidPnPresentPath;
    PDXGKDDI_RECOMMEND_MONITORMODES             DxgkDdiRecommendMonitorModes;
    PDXGKDDI_RECOMMEND_VIDPN_TOPOLOGY           DxgkDdiRecommendVidPnTopology;

    /* ---- Scan-line / interrupt / overlay -------------------------------- */
    PDXGKDDI_GET_SCAN_LINE                      DxgkDdiGetScanLine;
    PDXGKDDI_STOP_CAPTURE                       DxgkDdiStopCapture;
    PDXGKDDI_CONTROL_INTERRUPT                  DxgkDdiControlInterrupt;
    PDXGKDDI_CREATE_OVERLAY                     DxgkDdiCreateOverlay;

    /* ---- Per-device / per-context callbacks ----------------------------- */
    PDXGKDDI_DESTROY_DEVICE                     DxgkDdiDestroyDevice;
    PDXGKDDI_OPEN_ALLOCATION                    DxgkDdiOpenAllocation;
    PDXGKDDI_CLOSE_ALLOCATION                   DxgkDdiCloseAllocation;
    PDXGKDDI_RENDER                             DxgkDdiRender;
    PDXGKDDI_PRESENT                            DxgkDdiPresent;
    PDXGKDDI_UPDATE_OVERLAY                     DxgkDdiUpdateOverlay;
    PDXGKDDI_FLIP_OVERLAY                       DxgkDdiFlipOverlay;
    PDXGKDDI_DESTROY_OVERLAY                    DxgkDdiDestroyOverlay;
    PDXGKDDI_CREATE_CONTEXT                     DxgkDdiCreateContext;
    PDXGKDDI_DESTROY_CONTEXT                    DxgkDdiDestroyContext;
    PDXGKDDI_LINK_DEVICE                        DxgkDdiLinkDevice;
    PDXGKDDI_SET_DISPLAY_PRIVATE_DRIVER_FORMAT  DxgkDdiSetDisplayPrivateDriverFormat;

    /* ---- WDDM 1.1 / Win7 additions (optional for Vista drivers) --------- */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    PVOID                                       DxgkDdiDescribePageTable;        /* reserved, set to zero */
    PVOID                                       DxgkDdiUpdatePageTable;          /* reserved, set to zero */
    PVOID                                       DxgkDdiUpdatePageDirectory;      /* reserved, set to zero */
    PVOID                                       DxgkDdiMovePageDirectory;        /* reserved, set to zero */
    PVOID                                       DxgkDdiSubmitRender;             /* reserved, set to zero */
    PVOID                                       DxgkDdiCreateAllocation2;        /* reserved, set to zero */
    PDXGKDDI_RENDER                             DxgkDdiRenderKm;
    PVOID                                       Reserved;                        /* reserved, set to zero */
    PDXGKDDI_QUERY_VIDPN_HW_CAPABILITY          DxgkDdiQueryVidPnHWCapability;
#endif

    /* ---- WDDM 1.2 / Win8 additions -------------------------------------- */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    PVOID                                       DxgkDdiSetPowerComponentFState;
    PVOID                                       DxgkDdiQueryDependentEngineGroup;
    PDXGKDDI_QUERY_ENGINE_STATUS                DxgkDdiQueryEngineStatus;
    PDXGKDDI_RESET_ENGINE                       DxgkDdiResetEngine;
    PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE              DxgkDdiSystemDisplayEnable;
    PDXGKDDI_SYSTEM_DISPLAY_WRITE               DxgkDdiSystemDisplayWrite;
    PDXGKDDI_CANCEL_COMMAND                     DxgkDdiCancelCommand;
    PVOID                                       DxgkDdiGetChildContainerId;
    PVOID                                       DxgkDdiPowerRuntimeControlRequest;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay;
    PDXGKDDI_NOTIFY_SURPRISE_REMOVAL            DxgkDdiNotifySurpriseRemoval;
#endif

    /* ---- WDDM 1.3 / Win8.1 additions ------------------------------------ */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    PDXGKDDI_GET_NODE_METADATA                  DxgkDdiGetNodeMetadata;
    PVOID                                       DxgkDdiSetPowerPState;           /* reserved, set to zero */
    PDXGKDDI_CONTROLINTERRUPT2                   DxgkDdiControlInterrupt2;
    PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT      DxgkDdiCheckMultiPlaneOverlaySupport;
    PVOID                                       DxgkDdiCalibrateGpuClock;
    PVOID                                       DxgkDdiFormatHistoryBuffer;
#endif

    /* ---- WDDM 2.0 / Win10 additions ------------------------------------- */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    PDXGKDDI_RENDERGDI                                              DxgkDdiRenderGdi;
    PDXGKDDI_SUBMITCOMMANDVIRTUAL                                   DxgkDdiSubmitCommandVirtual;
    PDXGKDDI_SETROOTPAGETABLE                                       DxgkDdiSetRootPageTable;
    PDXGKDDI_GETROOTPAGETABLESIZE                                   DxgkDdiGetRootPageTableSize;
    PDXGKDDI_MAPCPUHOSTAPERTURE                                     DxgkDdiMapCpuHostAperture;
    PDXGKDDI_UNMAPCPUHOSTAPERTURE                                   DxgkDdiUnmapCpuHostAperture;
    PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT2                         DxgkDdiCheckMultiPlaneOverlaySupport2;
    PDXGKDDI_CREATEPROCESS                                          DxgkDdiCreateProcess;
    PDXGKDDI_DESTROYPROCESS                                         DxgkDdiDestroyProcess;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2             DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2;
    PVOID                                                           Reserved1;                       /* reserved */
    PVOID                                                           Reserved2;                       /* reserved */
    PDXGKDDI_POWERRUNTIMESETDEVICEHANDLE                            DxgkDdiPowerRuntimeSetDeviceHandle;
    PDXGKDDI_SETSTABLEPOWERSTATE                                    DxgkDdiSetStablePowerState;
    PDXGKDDI_SETVIDEOPROTECTEDREGION                                DxgkDdiSetVideoProtectedRegion;
#endif

    /* ---- WDDM 2.1 additions --------------------------------------------- */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3                         DxgkDdiCheckMultiPlaneOverlaySupport3;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3             DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3;
    PDXGKDDI_POSTMULTIPLANEOVERLAYPRESENT                            DxgkDdiPostMultiPlaneOverlayPresent;
    PDXGKDDI_VALIDATEUPDATEALLOCATIONPROPERTY                        DxgkDdiValidateUpdateAllocationProperty;
    PDXGKDDI_CONTROLMODEBEHAVIOR                                     DxgkDdiControlModeBehavior;
    PDXGKDDI_UPDATEMONITORLINKINFO                                   DxgkDdiUpdateMonitorLinkInfo;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    PDXGKDDI_CREATEHWCONTEXT                                         DxgkDdiCreateHwContext;
    PDXGKDDI_DESTROYHWCONTEXT                                        DxgkDdiDestroyHwContext;
    PDXGKDDI_CREATEHWQUEUE                                           DxgkDdiCreateHwQueue;
    PDXGKDDI_DESTROYHWQUEUE                                          DxgkDdiDestroyHwQueue;
    PDXGKDDI_SUBMITCOMMANDTOHWQUEUE                                  DxgkDdiSubmitCommandToHwQueue;
    PDXGKDDI_SWITCHTOHWCONTEXTLIST                                   DxgkDdiSwitchToHwContextList;
    PDXGKDDI_RESETHWENGINE                                           DxgkDdiResetHwEngine;
    PDXGKDDI_CREATEPERIODICFRAMENOTIFICATION                         DxgkDdiCreatePeriodicFrameNotification;
    PDXGKDDI_DESTROYPERIODICFRAMENOTIFICATION                        DxgkDdiDestroyPeriodicFrameNotification;
    PDXGKDDI_SETTIMINGSFROMVIDPN                                     DxgkDdiSetTimingsFromVidPn;
    PDXGKDDI_SETTARGETGAMMA                                          DxgkDdiSetTargetGamma;
    PDXGKDDI_SETTARGETCONTENTTYPE                                    DxgkDdiSetTargetContentType;
    PDXGKDDI_SETTARGETANALOGCOPYPROTECTION                           DxgkDdiSetTargetAnalogCopyProtection;
    PDXGKDDI_SETTARGETADJUSTEDCOLORIMETRY                            DxgkDdiSetTargetAdjustedColorimetry;
    PDXGKDDI_DISPLAYDETECTCONTROL                                    DxgkDdiDisplayDetectControl;
    PDXGKDDI_QUERYCONNECTIONCHANGE                                   DxgkDdiQueryConnectionChange;
    PDXGKDDI_EXCHANGEPRESTARTINFO                                    DxgkDdiExchangePreStartInfo;
    PDXGKDDI_GETMULTIPLANEOVERLAYCAPS                                DxgkDdiGetMultiPlaneOverlayCaps;
    PDXGKDDI_GETPOSTCOMPOSITIONCAPS                                  DxgkDdiGetPostCompositionCaps;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    PDXGKDDI_UPDATEHWCONTEXTSTATE                                    DxgkDdiUpdateHwContextState;
    PDXGKDDI_CREATEPROTECTEDSESSION                                  DxgkDdiCreateProtectedSession;
    PDXGKDDI_DESTROYPROTECTEDSESSION                                 DxgkDdiDestroyProtectedSession;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    PDXGKDDI_SETSCHEDULINGLOGBUFFER                                  DxgkDdiSetSchedulingLogBuffer;
    PDXGKDDI_SETUPPRIORITYBANDS                                      DxgkDdiSetupPriorityBands;
    PDXGKDDI_NOTIFYFOCUSPRESENT                                      DxgkDdiNotifyFocusPresent;
    PDXGKDDI_SETCONTEXTSCHEDULINGPROPERTIES                          DxgkDdiSetContextSchedulingProperties;
    PDXGKDDI_SUSPENDCONTEXT                                          DxgkDdiSuspendContext;
    PDXGKDDI_RESUMECONTEXT                                           DxgkDdiResumeContext;
    PDXGKDDI_SETVIRTUALMACHINEDATA                                   DxgkDdiSetVirtualMachineData;
    PDXGKDDI_BEGINEXCLUSIVEACCESS                                    DxgkDdiBeginExclusiveAccess;
    PDXGKDDI_ENDEXCLUSIVEACCESS                                      DxgkDdiEndExclusiveAccess;
    PDXGKDDI_QUERYDIAGNOSTICTYPESSUPPORT                             DxgkDdiQueryDiagnosticTypesSupport;
    PDXGKDDI_CONTROLDIAGNOSTICREPORTING                              DxgkDdiControlDiagnosticReporting;
    PDXGKDDI_RESUMEHWENGINE                                          DxgkDdiResumeHwEngine;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    PDXGKDDI_SIGNALMONITOREDFENCE                                    DxgkDdiSignalMonitoredFence;
    PDXGKDDI_PRESENTTOHWQUEUE                                        DxgkDdiPresentToHwQueue;
    PDXGKDDI_VALIDATESUBMITCOMMAND                                   DxgkDdiValidateSubmitCommand;
    PDXGKDDI_SETTARGETADJUSTEDCOLORIMETRY2                           DxgkDdiSetTargetAdjustedColorimetry2;
    PDXGKDDI_SETTRACKEDWORKLOADPOWERLEVEL                            DxgkDdiSetTrackedWorkloadPowerLevel;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
    PDXGKDDI_SAVEMEMORYFORHOTUPDATE                                  DxgkDdiSaveMemoryForHotUpdate;
    PDXGKDDI_RESTOREMEMORYFORHOTUPDATE                               DxgkDdiRestoreMemoryForHotUpdate;
    PDXGKDDI_COLLECTDIAGNOSTICINFO                                   DxgkDdiCollectDiagnosticInfo;
    void                                                            *Reserved3;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
    PDXGKDDI_CONTROLINTERRUPT3                                       DxgkDdiControlInterrupt3;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    PDXGKDDI_SETFLIPQUEUELOGBUFFER                                  DxgkDdiSetFlipQueueLogBuffer;
    PDXGKDDI_UPDATEFLIPQUEUELOG                                     DxgkDdiUpdateFlipQueueLog;
    PDXGKDDI_CANCELQUEUEDFLIPS                                      DxgkDdiCancelQueuedFlips;
    PDXGKDDI_SETINTERRUPTTARGETPRESENTID                            DxgkDdiSetInterruptTargetPresentId;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
    PDXGKDDI_SETALLOCATIONBACKINGSTORE                              DxgkDdiSetAllocationBackingStore;
    PDXGKDDI_CREATECPUEVENT                                         DxgkDdiCreateCpuEvent;
    PDXGKDDI_DESTROYCPUEVENT                                        DxgkDdiDestroyCpuEvent;
    PDXGKDDI_CANCELFLIPS                                            DxgkDdiCancelFlips;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
    PDXGKDDI_CREATENATIVEFENCE                                      DxgkDdiCreateNativeFence;
    PDXGKDDI_DESTROYNATIVEFENCE                                     DxgkDdiDestroyNativeFence;
    PDXGKDDI_UPDATEMONITOREDVALUES                                  DxgkDdiUpdateMonitoredValues;
    PDXGKDDI_UPDATECURRENTVALUESFROMCPU                             DxgkDdiUpdateCurrentValuesFromCpu;
    PDXGKDDI_CREATEDOORBELL                                         DxgkDdiCreateDoorbell;
    PDXGKDDI_CONNECTDOORBELL                                        DxgkDdiConnectDoorbell;
    PDXGKDDI_DISCONNECTDOORBELL                                     DxgkDdiDisconnectDoorbell;
    PDXGKDDI_DESTROYDOORBELL                                        DxgkDdiDestroyDoorbell;
    PDXGKDDI_NOTIFYWORKSUBMISSION                                   DxgkDdiNotifyWorkSubmission;
    void                                                           *Reserved4;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
    PDXGKDDI_CREATEMEMORYBASIS                                      DxgkDdiCreateMemoryBasis;
    PDXGKDDI_DESTROYMEMORYBASIS                                     DxgkDdiDestroyMemoryBasis;
    PDXGKDDI_STARTDIRTYTRACKING                                     DxgkDdiStartDirtyTracking;
    PDXGKDDI_STOPDIRTYTRACKING                                      DxgkDdiStopDirtyTracking;
    PDXGKDDI_QUERYDIRTYBITDATA                                      DxgkDdiQueryDirtyBitData;
    PDXGKDDI_PREPARELIVEMIGRATION                                   DxgkDdiPrepareLiveMigration;
    PDXGKDDI_SAVEIMMUTABLEMIGRATIONDATA                             DxgkDdiSaveImmutableMigrationData;
    PDXGKDDI_SAVEMUTABLEMIGRATIONDATA                               DxgkDdiSaveMutableMigrationData;
    PDXGKDDI_ENDLIVEMIGRATION                                       DxgkDdiEndLiveMigration;
    PDXGKDDI_RESTOREIMMUTABLEMIGRATIONDATA                          DxgkDdiRestoreImmutableMigrationData;
    PDXGKDDI_RESTOREMUTABLEMIGRATIONDATA                            DxgkDdiRestoreMutableMigrationData;
    PDXGKDDI_WRITEVIRTUALIZEDINTERRUPT                              DxgkDdiWriteVirtualizedInterrupt;
    PDXGKDDI_SETVIRTUALGPURESOURCES2                                DxgkDdiSetVirtualGpuResources2;
    PDXGKDDI_SETVIRTUALFUNCTIONPAUSESTATE                           DxgkDdiSetVirtualFunctionPauseState;
    PDXGKDDI_OPENNATIVEFENCE                                        DxgkDdiOpenNativeFence;
    PDXGKDDI_CLOSENATIVEFENCE                                       DxgkDdiCloseNativeFence;
    PDXGKDDI_SETNATIVEFENCELOGBUFFER                                DxgkDdiSetNativeFenceLogBuffer;
    PDXGKDDI_UPDATENATIVEFENCELOGS                                  DxgkDdiUpdateNativeFenceLogs;
    PDXGKDDI_COLLECTDBGINFO2                                        DxgkDdiCollectDbgInfo2;
    PDXGKDDI_NOTIFYCONTEXTPRIORITYCHANGE                            DxgkDdiNotifyContextPriorityChange;
    PDXGKDDI_RESETDISPLAYENGINE                                     DxgkDdiResetDisplayEngine;
#endif

} DRIVER_INITIALIZATION_DATA, *PDRIVER_INITIALIZATION_DATA;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1) && (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_2)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCheckMultiPlaneOverlaySupport3) == 0x340);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateMonitorLinkInfo) == 0x368);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x370);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCheckMultiPlaneOverlaySupport3) == 0x1A0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateMonitorLinkInfo) == 0x1B4);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x1B8);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2) && (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_3)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateHwContext) == 0x370);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiGetPostCompositionCaps) == 0x400);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x408);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateHwContext) == 0x1B8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiGetPostCompositionCaps) == 0x200);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x204);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_4)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateHwContextState) == 0x408);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateProtectedSession) == 0x410);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProtectedSession) == 0x418);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x420);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateHwContextState) == 0x204);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateProtectedSession) == 0x208);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProtectedSession) == 0x20C);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x210);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_5)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetSchedulingLogBuffer) == 0x420);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetupPriorityBands) == 0x428);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiNotifyFocusPresent) == 0x430);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetContextSchedulingProperties) == 0x438);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSuspendContext) == 0x440);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeContext) == 0x448);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVirtualMachineData) == 0x450);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiBeginExclusiveAccess) == 0x458);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiEndExclusiveAccess) == 0x460);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryDiagnosticTypesSupport) == 0x468);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiControlDiagnosticReporting) == 0x470);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeHwEngine) == 0x478);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x480);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetSchedulingLogBuffer) == 0x210);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetupPriorityBands) == 0x214);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiNotifyFocusPresent) == 0x218);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetContextSchedulingProperties) == 0x21C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSuspendContext) == 0x220);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeContext) == 0x224);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVirtualMachineData) == 0x228);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiBeginExclusiveAccess) == 0x22C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiEndExclusiveAccess) == 0x230);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryDiagnosticTypesSupport) == 0x234);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiControlDiagnosticReporting) == 0x238);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResumeHwEngine) == 0x23C);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x240);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_6)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSignalMonitoredFence) == 0x480);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiPresentToHwQueue) == 0x488);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiValidateSubmitCommand) == 0x490);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTargetAdjustedColorimetry2) == 0x498);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTrackedWorkloadPowerLevel) == 0x4A0);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x4A8);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSignalMonitoredFence) == 0x240);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiPresentToHwQueue) == 0x244);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiValidateSubmitCommand) == 0x248);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTargetAdjustedColorimetry2) == 0x24C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetTrackedWorkloadPowerLevel) == 0x250);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x254);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_7)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSaveMemoryForHotUpdate) == 0x4A8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiRestoreMemoryForHotUpdate) == 0x4B0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCollectDiagnosticInfo) == 0x4B8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Reserved3) == 0x4C0);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x4C8);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSaveMemoryForHotUpdate) == 0x254);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiRestoreMemoryForHotUpdate) == 0x258);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCollectDiagnosticInfo) == 0x25C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Reserved3) == 0x260);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x264);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_9)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x4C8);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x4D0);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiControlInterrupt3) == 0x264);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x268);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM3_0)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetFlipQueueLogBuffer) == 0x4D0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateFlipQueueLog) == 0x4D8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelQueuedFlips) == 0x4E0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x4E8);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x4F0);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetFlipQueueLogBuffer) == 0x268);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiUpdateFlipQueueLog) == 0x26C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelQueuedFlips) == 0x270);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetInterruptTargetPresentId) == 0x274);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x278);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM3_1)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetAllocationBackingStore) == 0x4F0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateCpuEvent) == 0x4F8);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyCpuEvent) == 0x500);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x508);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x510);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetAllocationBackingStore) == 0x278);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateCpuEvent) == 0x27C);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyCpuEvent) == 0x280);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCancelFlips) == 0x284);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x288);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1) && \
    (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM3_2)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateNativeFence) == 0x510);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x558);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x560);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateNativeFence) == 0x288);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, Reserved4) == 0x2AC);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x2B0);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateMemoryBasis) == 0x560);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x600);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x608);
#else
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateMemoryBasis) == 0x2B0);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResetDisplayEngine) == 0x300);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 0x304);
#endif
#endif

#if defined(_WIN64) && (DXGKDDI_INTERFACE_VERSION == 0x5023)
C_ASSERT(sizeof(D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY) == 4);
C_ASSERT(sizeof(DXGK_CHILD_CAPABILITIES) == 16);
C_ASSERT(FIELD_OFFSET(DXGK_CHILD_CAPABILITIES, HpdAwareness) == 12);
C_ASSERT(sizeof(DXGK_CHILD_DESCRIPTOR) == 28);
C_ASSERT(FIELD_OFFSET(DXGK_CHILD_DESCRIPTOR, ChildCapabilities) == 4);
C_ASSERT(FIELD_OFFSET(DXGK_CHILD_DESCRIPTOR, AcpiUid) == 20);
C_ASSERT(FIELD_OFFSET(DXGK_CHILD_DESCRIPTOR, ChildUid) == 24);
C_ASSERT(sizeof(DXGK_CHILD_STATUS) == 16);
C_ASSERT(FIELD_OFFSET(DXGK_CHILD_STATUS, Miracast.MiracastMonitorType) == 12);
C_ASSERT(sizeof(DXGK_START_INFO) == 28);
C_ASSERT(FIELD_OFFSET(DXGK_START_INFO, RequiredDmaQueueEntry) == 0);
C_ASSERT(FIELD_OFFSET(DXGK_START_INFO, AdapterGuid) == 4);
C_ASSERT(FIELD_OFFSET(DXGK_START_INFO, AdapterLuid) == 20);
C_ASSERT(sizeof(DXGKRNL_INTERFACE) == 312);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, Size) == 0);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, Version) == 4);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DeviceHandle) == 8);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbEvalAcpiMethod) == 16);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbLogEtwEvent) == 168);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbExcludeAdapterAccess) == 176);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbCreateContextAllocation) == 184);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbCompletePStateTransition) == 256);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbMapContextAllocation) == 264);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAcquireHandleData) == 288);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbReleaseHandleData) == 296);
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbHardwareContentProtectionTeardown) == 304);
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 832);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDescribePageTable) == 496);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetPowerComponentFState) == 568);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiNotifySurpriseRemoval) == 656);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiGetNodeMetadata) == 664);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiRenderGdi) == 712);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSubmitCommandVirtual) == 720);
C_ASSERT(FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion) == 824);
#endif


/*
 * KMDDOD_INITIALIZATION_DATA — Display-Only Driver (DOD) initialization.
 *
 * This layout matches the Windows 8+ public dispmprt.h definition for KMDOD.
 * It is NOT a trimmed copy of DRIVER_INITIALIZATION_DATA with fields removed
 * mechanically.  The order diverges after QueryAdapterInfo and, critically,
 * DxgkDdiPresentDisplayOnly appears much earlier than in the full WDDM table.
 *
 * Matching the Windows layout is required for prebuilt DOD miniports such as
 * viogpudo, which pass this structure to DxgkInitializeDisplayOnlyDriver.
 */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
struct _KMDDOD_INITIALIZATION_DATA
{
    ULONG                                       Version;

    /* PnP / power lifecycle — same leading layout as full WDDM */
    PDXGKDDI_ADD_DEVICE                         DxgkDdiAddDevice;
    PDXGKDDI_START_DEVICE                       DxgkDdiStartDevice;
    PDXGKDDI_STOP_DEVICE                        DxgkDdiStopDevice;
    PDXGKDDI_REMOVE_DEVICE                      DxgkDdiRemoveDevice;
    PDXGKDDI_DISPATCH_IO_REQUEST                DxgkDdiDispatchIoRequest;
    PDXGKDDI_INTERRUPT_ROUTINE                  DxgkDdiInterruptRoutine;
    PDXGKDDI_DPC_ROUTINE                        DxgkDdiDpcRoutine;
    PDXGKDDI_QUERY_CHILD_RELATIONS              DxgkDdiQueryChildRelations;
    PDXGKDDI_QUERY_CHILD_STATUS                 DxgkDdiQueryChildStatus;
    PDXGKDDI_QUERY_DEVICE_DESCRIPTOR            DxgkDdiQueryDeviceDescriptor;
    PDXGKDDI_SET_POWER_STATE                    DxgkDdiSetPowerState;
    PDXGKDDI_NOTIFY_ACPI_EVENT                  DxgkDdiNotifyAcpiEvent;
    PDXGKDDI_RESET_DEVICE                       DxgkDdiResetDevice;
    PDXGKDDI_UNLOAD                             DxgkDdiUnload;
    PDXGKDDI_QUERY_INTERFACE                    DxgkDdiQueryInterface;
    PDXGKDDI_CONTROL_ETW_LOGGING                DxgkDdiControlEtwLogging;
    PDXGKDDI_QUERY_ADAPTER_INFO                 DxgkDdiQueryAdapterInfo;

    /* DOD-specific layout from the Windows KMDOD contract */
    PDXGKDDI_SET_PALETTE                        DxgkDdiSetPalette;
    PDXGKDDI_SET_POINTER_POSITION               DxgkDdiSetPointerPosition;
    PDXGKDDI_SET_POINTER_SHAPE                  DxgkDdiSetPointerShape;
    PDXGKDDI_ESCAPE                             DxgkDdiEscape;
    PDXGKDDI_COLLECT_DB_ENGINE_INFO             DxgkDdiCollectDbgInfo;

    /* VidPN management */
    PDXGKDDI_IS_SUPPORTED_VIDPN                 DxgkDdiIsSupportedVidPn;
    PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN         DxgkDdiRecommendFunctionalVidPn;
    PDXGKDDI_ENUM_VIDPN_COFUNC_MODALITY         DxgkDdiEnumVidPnCofuncModality;
    PDXGKDDI_SET_VIDPN_SOURCE_VISIBILITY        DxgkDdiSetVidPnSourceVisibility;
    PDXGKDDI_COMMIT_VIDPN                       DxgkDdiCommitVidPn;
    PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH   DxgkDdiUpdateActiveVidPnPresentPath;
    PDXGKDDI_RECOMMEND_MONITORMODES             DxgkDdiRecommendMonitorModes;
    PDXGKDDI_GET_SCAN_LINE                      DxgkDdiGetScanLine;
    PDXGKDDI_QUERY_VIDPN_HW_CAPABILITY          DxgkDdiQueryVidPnHWCapability;

    /* Win8+ display-only callbacks */
    PVOID                                       DxgkDdiPresentDisplayOnly;
    PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE              DxgkDdiSystemDisplayEnable;
    PDXGKDDI_SYSTEM_DISPLAY_WRITE               DxgkDdiSystemDisplayWrite;
    PVOID                                       DxgkDdiGetChildContainerId;
    PDXGKDDI_CONTROL_INTERRUPT                  DxgkDdiControlInterrupt;
    PVOID                                       DxgkDdiSetPowerComponentFState;
    PVOID                                       DxgkDdiPowerRuntimeControlRequest;
    PDXGKDDI_NOTIFY_SURPRISE_REMOVAL            DxgkDdiNotifySurpriseRemoval;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    PDXGKDDI_POWERRUNTIMESETDEVICEHANDLE        DxgkDdiPowerRuntimeSetDeviceHandle;
#endif
};

#ifdef _WIN64
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
C_ASSERT(sizeof(KMDDOD_INITIALIZATION_DATA) == 0x150);
#else
C_ASSERT(sizeof(KMDDOD_INITIALIZATION_DATA) == 0x148);
#endif
#else
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
C_ASSERT(sizeof(KMDDOD_INITIALIZATION_DATA) == 0xA8);
#else
C_ASSERT(sizeof(KMDDOD_INITIALIZATION_DATA) == 0xA4);
#endif
#endif
#endif


/* =========================================================================
 * DxgkInitialize / DxgkInitializeEx
 *
 * Entry points provided by Displib.  The miniport calls DxgkInitialize from
 * its DriverEntry to register with the display kernel subsystem.
 * =========================================================================
 */

/*
 * DxgkInitialize — Vista WDDM 1.0 entry point.
 * Displib resolves the private dxgkrnl initializer at runtime.
 */
NTSTATUS
APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT                 DriverObject,
    _In_ PUNICODE_STRING                RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA    DriverInitializationData);


#pragma warning(pop)

#endif /* _DISPMPRT_H_ */
