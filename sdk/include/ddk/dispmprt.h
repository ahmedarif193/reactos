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

/* Legacy video-port declarations retained by the WDK for compatibility. */
#ifndef _NTOSP_
#define _NTOSP_
typedef enum _EMULATOR_PORT_ACCESS_TYPE
{
    Uchar,
    Ushort,
    Ulong
} EMULATOR_PORT_ACCESS_TYPE, *PEMULATOR_PORT_ACCESS_TYPE;

typedef struct _EMULATOR_ACCESS_ENTRY
{
    ULONG BasePort;
    ULONG NumConsecutivePorts;
    EMULATOR_PORT_ACCESS_TYPE AccessType;
    UCHAR AccessMode;
    UCHAR StringSupport;
    PVOID Routine;
} EMULATOR_ACCESS_ENTRY, *PEMULATOR_ACCESS_ENTRY;
#endif

typedef VOID (*PBANKED_SECTION_ROUTINE)(
    _In_ ULONG ReadBank,
    _In_ ULONG WriteBank,
    _In_ PVOID Context);

/* dispmprt.h exposes the complete video request packet, as does the WDK. */
#ifndef _NTOSDEF_
#define _NTOSDEF_
#endif
#include <video.h>

#ifndef _IRQL_requires_DXGK_
#define _IRQL_requires_DXGK_(level) _IRQL_requires_(level)
#endif

/* Public display ACPI notifications, arguments, and method identifiers. */
#define ACPI_NOTIFY_DOCK_EVENT              0x77
#define ACPI_NOTIFY_PANEL_SWITCH            0x80
#define ACPI_NOTIFY_DEVICE_HOTPLUG          0x81
#define ACPI_NOTIFY_CYCLE_DISPLAY_HOTKEY    0x82
#define ACPI_NOTIFY_NEXT_DISPLAY_HOTKEY     0x83
#define ACPI_NOTIFY_PREV_DISPLAY_HOTKEY     0x84
#define ACPI_NOTIFY_CYCLE_BRIGHTNESS_HOTKEY 0x85
#define ACPI_NOTIFY_INC_BRIGHTNESS_HOTKEY   0x86
#define ACPI_NOTIFY_DEC_BRIGHTNESS_HOTKEY   0x87
#define ACPI_NOTIFY_ZERO_BRIGHTNESS_HOTKEY  0x88
#define ACPI_NOTIFY_VIDEO_WAKEUP            0x90

#define ACPI_ARG_ENABLE_SWITCH_EVENT        0x0
#define ACPI_ARG_ENABLE_AUTO_SWITCH         0x1
#define ACPI_ARG_DISABLE_SWITCH_EVENT       0x2
#define ACPI_ARG_ENABLE_AUTO_LCD_BRIGHTNESS 0x0
#define ACPI_ARG_DISABLE_AUTO_LCD_BRIGHTNESS 0x4

#define ACPI_METHOD_DISPLAY_DOS ((ULONG)('SOD_'))
#define ACPI_METHOD_DISPLAY_DOD ((ULONG)('DOD_'))
#define ACPI_METHOD_DISPLAY_ROM ((ULONG)('MOR_'))
#define ACPI_METHOD_DISPLAY_GPD ((ULONG)('DPG_'))
#define ACPI_METHOD_DISPLAY_SPD ((ULONG)('DPS_'))
#define ACPI_METHOD_DISPLAY_VPO ((ULONG)('OPV_'))
#define ACPI_METHOD_HARDWARE_ID ((ULONG)('DIH_'))
#define ACPI_METHOD_SUBSYSTEM_ID ((ULONG)('BUS_'))
#define ACPI_METHOD_REVISION_ID  ((ULONG)('VRH_'))

#define ACPI_METHOD_OUTPUT_ADR ((ULONG)('RDA_'))
#define ACPI_METHOD_OUTPUT_BCL ((ULONG)('LCB_'))
#define ACPI_METHOD_OUTPUT_BCM ((ULONG)('MCB_'))
#define ACPI_METHOD_OUTPUT_DDC ((ULONG)('CDD_'))
#define ACPI_METHOD_OUTPUT_DCS ((ULONG)('SCD_'))
#define ACPI_METHOD_OUTPUT_DGS ((ULONG)('SGD_'))
#define ACPI_METHOD_OUTPUT_DSS ((ULONG)('SSD_'))

#define DXGK_ACPI_POLL_DISPLAY_CHILDREN  0x00000001
#define DXGK_ACPI_CHANGE_DISPLAY_MODE    0x00000002
#define DXGK_ACPI_CHANGE_DISPLAY_TOPOLOGY 0x00000004
#define DXGK_ACPI_CHAIN_NOT_HANDLED      0x00000008

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

/* DxgkCbExcludeAdapterAccess Attributes. */
#define DXGK_EXCLUDE_EVICT_ALL          0x00000001
#define DXGK_EXCLUDE_CALL_SYNCHRONOUS   0x00000002
#define DXGK_EXCLUDE_BRIDGE_ACCESS      0x00000004
#define DXGK_EXCLUDE_EVICT_STANDBY        0x00000008
#define DXGK_EXCLUDE_EVICT_HIBERNATE      0x00000010
#define DXGK_EXCLUDE_EVICT_SHUTDOWN       0x00000020
#define DXGK_EXCLUDE_D3_STATE_TRANSITION  0x00000040
#define DXGK_EXCLUDE_EVICT_DFX_STANDBY    0x00000080

/* DXGK_INTERRUPT_TYPE and its typed enumerators are declared by d3dkmddi.h. */


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
} DXGK_CHILD_DEVICE_TYPE, *PDXGK_CHILD_DEVICE_TYPE;


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
} DXGK_VIDEO_OUTPUT_CAPABILITIES, *PDXGK_VIDEO_OUTPUT_CAPABILITIES;

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
} DXGK_CHILD_CAPABILITIES, *PDXGK_CHILD_CAPABILITIES;


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
} DXGK_CHILD_STATUS_TYPE, *PDXGK_CHILD_STATUS_TYPE;

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

#define DXGK_MAX_STRING_LEN 50
#define DXGK_MAX_REG_SZ_LEN (DXGK_MAX_STRING_LEN + 1)

typedef struct _DXGK_GENERIC_DESCRIPTOR
{
    WCHAR HardwareId[DXGK_MAX_REG_SZ_LEN];
    WCHAR InstanceId[DXGK_MAX_REG_SZ_LEN];
    WCHAR CompatibleId[DXGK_MAX_REG_SZ_LEN];
    WCHAR DeviceText[DXGK_MAX_REG_SZ_LEN];
} DXGK_GENERIC_DESCRIPTOR, *PDXGK_GENERIC_DESCRIPTOR;


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

DEFINE_GUID(GUID_DEVINTERFACE_OPM,
            0xbf4672de, 0x6b4e, 0x4be4, 0xa3, 0x25, 0x68, 0xa9, 0x1e, 0xa4, 0x9c, 0x09);

#define DXGK_OPM_INTERFACE_VERSION_1 0x01

typedef NTSTATUS (*DXGKDDI_OPM_GET_CERTIFICATE_SIZE)(
    PVOID MiniportDeviceContext,
    DXGKMDT_CERTIFICATE_TYPE CertificateType,
    PULONG CertificateSize);

typedef NTSTATUS (*DXGKDDI_OPM_GET_CERTIFICATE)(
    PVOID MiniportDeviceContext,
    DXGKMDT_CERTIFICATE_TYPE CertificateType,
    ULONG CertificateSize,
    PVOID CertificateBuffer);

typedef NTSTATUS (*DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT)(
    PVOID MiniportDeviceContext,
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    DXGKMDT_OPM_VIDEO_OUTPUT_SEMANTICS NewVideoOutputSemantics,
    PHANDLE NewProtectedOutputHandle);

typedef NTSTATUS (*DXGKDDI_OPM_GET_RANDOM_NUMBER)(
    PVOID MiniportDeviceContext,
    HANDLE ProtectedOutputHandle,
    PDXGKMDT_OPM_RANDOM_NUMBER RandomNumber);

typedef NTSTATUS (*DXGKDDI_OPM_SET_SIGNING_KEY_AND_SEQUENCE_NUMBERS)(
    PVOID MiniportDeviceContext,
    HANDLE ProtectedOutputHandle,
    CONST DXGKMDT_OPM_ENCRYPTED_PARAMETERS *EncryptedParameters);

typedef NTSTATUS (*DXGKDDI_OPM_GET_INFORMATION)(
    PVOID MiniportDeviceContext,
    HANDLE ProtectedOutputHandle,
    CONST DXGKMDT_OPM_GET_INFO_PARAMETERS *Parameters,
    PDXGKMDT_OPM_REQUESTED_INFORMATION RequestedInformation);

typedef NTSTATUS (*DXGKDDI_OPM_GET_COPP_COMPATIBLE_INFORMATION)(
    PVOID MiniportDeviceContext,
    HANDLE ProtectedOutputHandle,
    CONST DXGKMDT_OPM_COPP_COMPATIBLE_GET_INFO_PARAMETERS *Parameters,
    PDXGKMDT_OPM_REQUESTED_INFORMATION RequestedInformation);

typedef NTSTATUS (*DXGKDDI_OPM_CONFIGURE_PROTECTED_OUTPUT)(
    PVOID MiniportDeviceContext,
    HANDLE ProtectedOutputHandle,
    CONST DXGKMDT_OPM_CONFIGURE_PARAMETERS *Parameters,
    ULONG AdditionalParametersSize,
    CONST VOID *AdditionalParameters);

typedef NTSTATUS (*DXGKDDI_OPM_DESTROY_PROTECTED_OUTPUT)(
    PVOID MiniportDeviceContext,
    HANDLE ProtectedOutputHandle);

typedef struct _DXGK_OPM_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_OPM_GET_CERTIFICATE_SIZE DxgkDdiOPMGetCertificateSize;
    DXGKDDI_OPM_GET_CERTIFICATE DxgkDdiOPMGetCertificate;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT DxgkDdiOPMCreateProtectedOutput;
    DXGKDDI_OPM_GET_RANDOM_NUMBER DxgkDdiOPMGetRandomNumber;
    DXGKDDI_OPM_SET_SIGNING_KEY_AND_SEQUENCE_NUMBERS DxgkDdiOPMSetSigningKeyAndSequenceNumbers;
    DXGKDDI_OPM_GET_INFORMATION DxgkDdiOPMGetInformation;
    DXGKDDI_OPM_GET_COPP_COMPATIBLE_INFORMATION DxgkDdiOPMGetCOPPCompatibleInformation;
    DXGKDDI_OPM_CONFIGURE_PROTECTED_OUTPUT DxgkDdiOPMConfigureProtectedOutput;
    DXGKDDI_OPM_DESTROY_PROTECTED_OUTPUT DxgkDdiOPMDestroyProtectedOutput;
} DXGK_OPM_INTERFACE, *PDXGK_OPM_INTERFACE;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
DEFINE_GUID(GUID_DEVINTERFACE_OPM_2_JTP,
            0xe929eea4, 0xb9f1, 0x407b, 0xaa, 0xb9, 0xab, 0x08, 0xbb, 0x44, 0xfb, 0xf4);
DEFINE_GUID(GUID_DEVINTERFACE_OPM_2,
            0x7f098726, 0x2ebb, 0x4ff3, 0xa2, 0x7f, 0x10, 0x46, 0xb9, 0x5d, 0xc5, 0x17);

#define DXGK_OPM_INTERFACE_VERSION_2_JTP 0x02
#define DXGK_OPM_INTERFACE_VERSION_2 0x03

typedef NTSTATUS (*DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT_NONLOCAL_DISPLAY_JTP)(
    PVOID MiniportDeviceContext,
    DXGKMDT_OPM_VIDEO_OUTPUT_SEMANTICS NewVideoOutputSemantics,
    ULONG64 OPMEncoderContext,
    DXGKMDT_OPM_ACTUAL_OUTPUT_FORMAT *pActualOutputFormat,
    D3DDDI_VIDEO_PRESENT_TARGET_ID NonLocalOutputId,
    PHANDLE NewProtectedOutputHandle);

typedef NTSTATUS (*DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT_VIRTUAL_MODE_JTP)(
    PVOID MiniportDeviceContext,
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    DXGKMDT_OPM_VIDEO_OUTPUT_SEMANTICS NewVideoOutputSemantics,
    DXGKMDT_OPM_ACTUAL_OUTPUT_FORMAT *pActualOutputFormat,
    PHANDLE NewProtectedOutputHandle);

typedef struct _DXGK_OPM_INTERFACE_2_JTP
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_OPM_GET_CERTIFICATE_SIZE DxgkDdiOPMGetCertificateSize;
    DXGKDDI_OPM_GET_CERTIFICATE DxgkDdiOPMGetCertificate;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT DxgkDdiOPMCreateProtectedOutput;
    DXGKDDI_OPM_GET_RANDOM_NUMBER DxgkDdiOPMGetRandomNumber;
    DXGKDDI_OPM_SET_SIGNING_KEY_AND_SEQUENCE_NUMBERS DxgkDdiOPMSetSigningKeyAndSequenceNumbers;
    DXGKDDI_OPM_GET_INFORMATION DxgkDdiOPMGetInformation;
    DXGKDDI_OPM_GET_COPP_COMPATIBLE_INFORMATION DxgkDdiOPMGetCOPPCompatibleInformation;
    DXGKDDI_OPM_CONFIGURE_PROTECTED_OUTPUT DxgkDdiOPMConfigureProtectedOutput;
    DXGKDDI_OPM_DESTROY_PROTECTED_OUTPUT DxgkDdiOPMDestroyProtectedOutput;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT_VIRTUAL_MODE_JTP DxgkDdiOPMCreateProtectedOutputVirtualMode;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT_NONLOCAL_DISPLAY_JTP DxgkDdiOPMCreateProtectedOutputNonLocalDisplay;
} DXGK_OPM_INTERFACE_2_JTP, *PDXGK_OPM_INTERFACE_2_JTP;

typedef NTSTATUS (*DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT_NONLOCAL_DISPLAY)(
    PVOID MiniportDeviceContext,
    DXGKMDT_OPM_VIDEO_OUTPUT_SEMANTICS NewVideoOutputSemantics,
    UINT64 OPMEncoderContext,
    DXGKMDT_OPM_ACTUAL_OUTPUT_FORMAT *pActualOutputFormat,
    UINT64 NonLocalOutputId,
    DXGKMDT_OPM_CONNECTOR_TYPE NonLocalConnectorType,
    PHANDLE NewProtectedOutputHandle);

typedef struct _DXGK_OPM_INTERFACE_2
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_OPM_GET_CERTIFICATE_SIZE DxgkDdiOPMGetCertificateSize;
    DXGKDDI_OPM_GET_CERTIFICATE DxgkDdiOPMGetCertificate;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT DxgkDdiOPMCreateProtectedOutput;
    DXGKDDI_OPM_GET_RANDOM_NUMBER DxgkDdiOPMGetRandomNumber;
    DXGKDDI_OPM_SET_SIGNING_KEY_AND_SEQUENCE_NUMBERS DxgkDdiOPMSetSigningKeyAndSequenceNumbers;
    DXGKDDI_OPM_GET_INFORMATION DxgkDdiOPMGetInformation;
    DXGKDDI_OPM_GET_COPP_COMPATIBLE_INFORMATION DxgkDdiOPMGetCOPPCompatibleInformation;
    DXGKDDI_OPM_CONFIGURE_PROTECTED_OUTPUT DxgkDdiOPMConfigureProtectedOutput;
    DXGKDDI_OPM_DESTROY_PROTECTED_OUTPUT DxgkDdiOPMDestroyProtectedOutput;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT_NONLOCAL_DISPLAY DxgkDdiOPMCreateProtectedOutputNonLocalDisplay;
} DXGK_OPM_INTERFACE_2, *PDXGK_OPM_INTERFACE_2;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
DEFINE_GUID(GUID_DEVINTERFACE_OPM_3,
            0x693a2cb1, 0x8c8d, 0x4ab6, 0x95, 0x55, 0x4b, 0x85, 0xef, 0x2c, 0x7c, 0x6b);

#define DXGK_OPM_INTERFACE_VERSION_3 0x04

typedef NTSTATUS (*DXGKDDI_OPM_SET_SRM_LIST)(
    PVOID MiniportDeviceContext,
    ULONG SrmListSize,
    PVOID SrmListBuffer);

typedef NTSTATUS (*DXGKDDI_OPM_GET_SRM_LIST_VERSION)(
    PVOID MiniportDeviceContext,
    PULONG SrmListVersionSize,
    PVOID SrmListVersionBuffer);

typedef struct _DXGK_OPM_INTERFACE_3
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_OPM_GET_CERTIFICATE_SIZE DxgkDdiOPMGetCertificateSize;
    DXGKDDI_OPM_GET_CERTIFICATE DxgkDdiOPMGetCertificate;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT DxgkDdiOPMCreateProtectedOutput;
    DXGKDDI_OPM_GET_RANDOM_NUMBER DxgkDdiOPMGetRandomNumber;
    DXGKDDI_OPM_SET_SIGNING_KEY_AND_SEQUENCE_NUMBERS DxgkDdiOPMSetSigningKeyAndSequenceNumbers;
    DXGKDDI_OPM_GET_INFORMATION DxgkDdiOPMGetInformation;
    DXGKDDI_OPM_GET_COPP_COMPATIBLE_INFORMATION DxgkDdiOPMGetCOPPCompatibleInformation;
    DXGKDDI_OPM_CONFIGURE_PROTECTED_OUTPUT DxgkDdiOPMConfigureProtectedOutput;
    DXGKDDI_OPM_DESTROY_PROTECTED_OUTPUT DxgkDdiOPMDestroyProtectedOutput;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT_NONLOCAL_DISPLAY DxgkDdiOPMCreateProtectedOutputNonLocalDisplay;
    DXGKDDI_OPM_SET_SRM_LIST DxgkDdiOPMSetSrmList;
    DXGKDDI_OPM_GET_SRM_LIST_VERSION DxgkDdiOPMGetSrmListVersion;
} DXGK_OPM_INTERFACE_3, *PDXGK_OPM_INTERFACE_3;
#endif

DEFINE_GUID(GUID_DEVINTERFACE_I2C,
            0x2564aa4f, 0xdddb, 0x4495, 0xb4, 0x97, 0x6a, 0xd4, 0xa8, 0x41, 0x63, 0xd7);

#define DXGK_I2C_INTERFACE_VERSION_1 0x01

typedef NTSTATUS (*DXGKDDI_I2C_TRANSMIT_DATA_TO_DISPLAY)(
    PVOID MiniportDeviceContext,
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    ULONG SevenBitI2CAddress,
    ULONG DataLength,
    CONST VOID *Data);

typedef NTSTATUS (*DXGKDDI_I2C_RECEIVE_DATA_FROM_DISPLAY)(
    PVOID MiniportDeviceContext,
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    ULONG SevenBitI2CAddress,
    ULONG Flags,
    ULONG DataLength,
    PVOID Data);

typedef struct _DXGK_I2C_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_I2C_TRANSMIT_DATA_TO_DISPLAY DxgkDdiI2CTransmitDataToDisplay;
    DXGKDDI_I2C_RECEIVE_DATA_FROM_DISPLAY DxgkDdiI2CReceiveDataFromDisplay;
} DXGK_I2C_INTERFACE, *PDXGK_I2C_INTERFACE;

#define DXGK_BRIGHTNESS_INTERFACE_VERSION_1 0x01

typedef NTSTATUS (*DXGK_BRIGHTNESS_GET_POSSIBLE)(
    PVOID Context,
    ULONG BufferSize,
    PUCHAR LevelCount,
    PUCHAR BrightnessLevels);

typedef NTSTATUS (*DXGK_BRIGHTNESS_SET)(
    PVOID Context,
    UCHAR Brightness);

typedef NTSTATUS (*DXGK_BRIGHTNESS_GET)(
    PVOID Context,
    PUCHAR Brightness);

typedef struct
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGK_BRIGHTNESS_GET_POSSIBLE GetPossibleBrightness;
    DXGK_BRIGHTNESS_SET SetBrightness;
    DXGK_BRIGHTNESS_GET GetBrightness;
} DXGK_BRIGHTNESS_INTERFACE, *PDXGK_BRIGHTNESS_INTERFACE;

#define DXGK_BRIGHTNESS_INTERFACE_VERSION_2 0x02

typedef NTSTATUS (*DXGK_BRIGHTNESS_GET_CAPS)(
    PVOID Context,
    DXGK_BRIGHTNESS_CAPS *BrightnessCaps);

typedef NTSTATUS (*DXGK_BRIGHTNESS_SET_STATE)(
    PVOID Context,
    DXGK_BRIGHTNESS_STATE *BrightnessState);

typedef NTSTATUS (*DXGK_BRIGHTNESS_SET_BACKLIGHT_OPTIMIZATION)(
    PVOID Context,
    DXGK_BACKLIGHT_OPTIMIZATION_LEVEL OptimizationLevel);

typedef NTSTATUS (*DXGK_BRIGHTNESS_GET_BACKLIGHT_REDUCTION)(
    PVOID Context,
    DXGK_BACKLIGHT_INFO *BacklightInfo);

typedef struct
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGK_BRIGHTNESS_GET_POSSIBLE GetPossibleBrightness;
    DXGK_BRIGHTNESS_SET SetBrightness;
    DXGK_BRIGHTNESS_GET GetBrightness;
    DXGK_BRIGHTNESS_GET_CAPS GetBrightnessCaps;
    DXGK_BRIGHTNESS_SET_STATE SetBrightnessState;
    DXGK_BRIGHTNESS_SET_BACKLIGHT_OPTIMIZATION SetBacklightOptimization;
    DXGK_BRIGHTNESS_GET_BACKLIGHT_REDUCTION GetBacklightReduction;
} DXGK_BRIGHTNESS_INTERFACE_2, *PDXGK_BRIGHTNESS_INTERFACE_2;

#define DXGK_BRIGHTNESS_INTERFACE_VERSION_3 0x03

typedef NTSTATUS (*DXGK_BRIGHTNESS_SET_3)(
    PVOID Context,
    ULONG ChildUid,
    PDXGK_BRIGHTNESS_SET_IN pIn);

typedef NTSTATUS (*DXGK_BRIGHTNESS_GET_3)(
    PVOID Context,
    ULONG ChildUid,
    PDXGK_BRIGHTNESS_GET_OUT pOut);

typedef NTSTATUS (*DXGK_BRIGHTNESS_GET_CAPS_3)(
    PVOID Context,
    ULONG ChildUid,
    DXGK_BRIGHTNESS_CAPS *pBrightnessCaps);

typedef NTSTATUS (*DXGK_BRIGHTNESS_GET_NIT_RANGES)(
    PVOID Context,
    ULONG ChildUid,
    PDXGK_BRIGHTNESS_GET_NIT_RANGES_OUT pOut);

typedef NTSTATUS (*DXGK_BRIGHTNESS_SET_BACKLIGHT_OPTIMIZATION_3)(
    PVOID Context,
    ULONG ChildUid,
    DXGK_BACKLIGHT_OPTIMIZATION_LEVEL OptimizationLevel);

typedef struct
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGK_BRIGHTNESS_SET_3 SetBrightness;
    DXGK_BRIGHTNESS_GET_3 GetBrightness;
    DXGK_BRIGHTNESS_GET_CAPS_3 GetBrightnessCaps;
    DXGK_BRIGHTNESS_GET_NIT_RANGES GetNitRanges;
    DXGK_BRIGHTNESS_SET_BACKLIGHT_OPTIMIZATION_3 SetBacklightOptimization;
} DXGK_BRIGHTNESS_INTERFACE_3, *PDXGK_BRIGHTNESS_INTERFACE_3;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
DEFINE_GUID(GUID_DEVINTERFACE_MIRACAST_DISPLAY,
            0xaf03f190, 0x22af, 0x48cb, 0x94, 0xbb, 0xb7, 0x8e, 0x76, 0xa2, 0x51, 0x07);

#define DXGK_MIRACAST_DISPLAY_INTERFACE_VERSION_1 0x01

typedef struct _DXGK_MIRACAST_CAPS
{
    ULONG MaxChunkPrivateDriverDataSize;
    union
    {
        struct
        {
            UINT HdcpSupport : 1;
            UINT Reserved : 31;
        };
        UINT Value;
    } Flags;
} DXGK_MIRACAST_CAPS, *PDXGK_MIRACAST_CAPS;

typedef NTSTATUS (*DXGKDDI_MIRACAST_QUERY_CAPS)(
    PVOID DriverContext,
    ULONG MiracastCapsSize,
    DXGK_MIRACAST_CAPS *MiracastCaps);

typedef VOID (*DXGKCB_MIRACAST_SEND_MESSAGE_CALLBACK)(
    PVOID CallbackContext,
    PIO_STATUS_BLOCK pIoStatusBlock);

typedef NTSTATUS (*DXGKCB_MIRACAST_SEND_MESSAGE)(
    HANDLE MiracastHandle,
    ULONG InputBufferSize,
    VOID *pInputBuffer,
    ULONG OutputBufferSize,
    VOID *pOutputBuffer,
    DXGKCB_MIRACAST_SEND_MESSAGE_CALLBACK pCallback,
    PVOID pCallbackContext);

typedef NTSTATUS (*DXGKCB_MIRACAST_REPORT_CHUNK_INFO)(
    HANDLE MiracastHandle,
    DXGK_MIRACAST_CHUNK_INFO *pChunkInfo,
    PVOID pPrivateDriverData,
    UINT PrivateDataDriverSize);

typedef struct _DXGK_MIRACAST_DISPLAY_CALLBACKS
{
    HANDLE MiracastHandle;
    DXGKCB_MIRACAST_SEND_MESSAGE DxgkCbMiracastSendMessage;
    DXGKCB_MIRACAST_REPORT_CHUNK_INFO DxgkCbReportChunkInfo;
} DXGK_MIRACAST_DISPLAY_CALLBACKS, *PDXGK_MIRACAST_DISPLAY_CALLBACKS;

typedef NTSTATUS (*DXGKDDI_MIRACAST_CREATE_CONTEXT)(
    PVOID DriverContext,
    DXGK_MIRACAST_DISPLAY_CALLBACKS *MiracastCallbacks,
    PVOID *MiracastContext,
    ULONG *TargetId);

typedef VOID (*DXGKDDI_MIRACAST_DESTROY_CONTEXT)(
    PVOID DriverContext,
    PVOID MiracastContext);

typedef NTSTATUS (*DXGKDDI_MIRACAST_HANDLE_IO_CONTROL)(
    PVOID DriverContext,
    PVOID MiracastContext,
    ULONG InputBufferSize,
    VOID *pInputBuffer,
    ULONG OutputBufferSize,
    VOID *pOutputBuffer,
    ULONG *BytesReturned);

typedef struct _DXGK_MIRACAST_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_MIRACAST_QUERY_CAPS DxgkDdiMiracastQueryCaps;
    DXGKDDI_MIRACAST_CREATE_CONTEXT DxgkDdiMiracastCreateContext;
    DXGKDDI_MIRACAST_HANDLE_IO_CONTROL DxgkDdiMiracastIoControl;
    DXGKDDI_MIRACAST_DESTROY_CONTEXT DxgkDdiMiracastDestroyContext;
} DXGK_MIRACAST_DISPLAY_INTERFACE, *PDXGK_MIRACAST_DISPLAY_INTERFACE;
#endif

#define DXGK_AGP_INTERFACE_VERSION_1 0x01
#define DXGK_AGPCOMMAND_AGP1X 0x00001
#define DXGK_AGPCOMMAND_AGP2X 0x00002
#define DXGK_AGPCOMMAND_AGP4X 0x00004
#define DXGK_AGPCOMMAND_AGP8X 0x00008
#define DXGK_AGPCOMMAND_DISABLE_SBA 0x10000
#define DXGK_AGPCOMMAND_DISABLE_FW 0x20000

typedef NTSTATUS (APIENTRY *DXGKCB_AGP_ALLOCATE_POOL)(
    HANDLE Context,
    ULONG AllocationSize,
    MEMORY_CACHING_TYPE CacheType,
    PPHYSICAL_ADDRESS PhysicalAddress,
    PVOID *VirtualAddress);

typedef NTSTATUS (APIENTRY *DXGKCB_AGP_FREE_POOL)(
    HANDLE Context,
    PVOID VirtualAddress);

typedef NTSTATUS (APIENTRY *DXGKCB_AGP_SET_COMMAND)(
    HANDLE Context,
    ULONG Command);

typedef struct _DXGK_AGP_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKCB_AGP_ALLOCATE_POOL AgpAllocatePool;
    DXGKCB_AGP_FREE_POOL AgpFreePool;
    DXGKCB_AGP_SET_COMMAND AgpSetCommand;
} DXGK_AGP_INTERFACE, *PDXGK_AGP_INTERFACE;

#define DXGK_TIMED_OPERATION_INTERFACE_VERSION_1 0x01
#define DXGK_TIMED_OPERATION_TIMEOUT_MAX_SECONDS 5

typedef struct _DXGK_TIMED_OPERATION
{
    USHORT Size;
    ULONG_PTR OwnerTag;
    BOOLEAN OsHandled;
    BOOLEAN TimeoutTriggered;
    LARGE_INTEGER Timeout;
    LARGE_INTEGER StartTick;
} DXGK_TIMED_OPERATION, *PDXGK_TIMED_OPERATION;

typedef struct _DXGK_TIMED_OPERATION_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    NTSTATUS (*TimedOperationStart)(DXGK_TIMED_OPERATION *Op, const LARGE_INTEGER *Timeout, BOOLEAN OsHandled);
    NTSTATUS (*TimedOperationDelay)(DXGK_TIMED_OPERATION *Op, KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, const LARGE_INTEGER *Interval);
    NTSTATUS (*TimedOperationWaitForSingleObject)(DXGK_TIMED_OPERATION *Op, PVOID Object, KWAIT_REASON WaitReason, KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, const LARGE_INTEGER *Timeout);
} DXGK_TIMED_OPERATION_INTERFACE, *PDXGK_TIMED_OPERATION_INTERFACE;

#define DXGK_SPB_INTERFACE_VERSION_1 0x01

typedef struct _DXGK_SPB_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    NTSTATUS (*OpenSpbResource)(HANDLE DeviceHandle, LARGE_INTEGER SpbResourceId, UNICODE_STRING *SpbResourceSubName, ACCESS_MASK DesiredAccess, ULONG ShareAccess, ULONG OpenOptions, VOID **SpbResource);
    NTSTATUS (*CloseSpbResource)(HANDLE DeviceHandle, VOID *SpbResource);
    NTSTATUS (*ReadSpbResource)(HANDLE DeviceHandle, VOID *SpbResource, ULONG Length, VOID *Buffer, LARGE_INTEGER *ByteOffset, HANDLE EventHandle, IO_STATUS_BLOCK *IoStatusBlock);
    NTSTATUS (*WriteSpbResource)(HANDLE DeviceHandle, VOID *SpbResource, ULONG Length, VOID *Buffer, LARGE_INTEGER *ByteOffset, HANDLE EventHandle, IO_STATUS_BLOCK *IoStatusBlock);
    NTSTATUS (*SpbResourceIoControl)(HANDLE DeviceHandle, VOID *SpbResource, ULONG IoControlCode, ULONG InBufferSize, VOID *InputBuffer, ULONG OutBufferSize, VOID *OutputBuffer, HANDLE EventHandle, IO_STATUS_BLOCK *IoStatusBlock);
} DXGK_SPB_INTERFACE, *PDXGK_SPB_INTERFACE;

#define DXGK_FIRMWARE_TABLE_INTERFACE_VERSION_1 0x01

typedef struct _DXGK_FIRMWARE_TABLE_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    NTSTATUS (*EnumSystemFirmwareTables)(VOID *Context, ULONG ProviderSignature, ULONG BufferSize, VOID *Buffer, ULONG *RequiredSize);
    NTSTATUS (*ReadSystemFirmwareTable)(VOID *Context, ULONG ProviderSignature, ULONG TableId, ULONG BufferSize, VOID *Buffer, ULONG *RequiredSize);
} DXGK_FIRMWARE_TABLE_INTERFACE, *PDXGK_FIRMWARE_TABLE_INTERFACE;

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
} DXGK_EVENT_TYPE, *PDXGK_EVENT_TYPE;

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

/* Signature used by display miniports when ACPI arguments can target child
 * devices.  DxgkCbEvalAcpiMethod restores the ordinary complex signature
 * before returning. */
#ifndef DXGK_ACPI_PASS_ARGS_TO_CHILDREN
#define DXGK_ACPI_PASS_ARGS_TO_CHILDREN 'araP'
#endif

#define DXGK_ACPI_USE_ACPI_UID ' diU'
#define DXGK_ACPI_USE_EVAL_EX 'xEvE'


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

/* Public WDDM debug-report service returned by DxgkCbQueryServices. */
DECLARE_HANDLE(DXGK_DEBUG_REPORT_HANDLE);

#define DXGK_DEBUG_REPORT_INTERFACE_VERSION_1 0x01
#define DXGK_DEBUG_REPORT_MAX_SIZE             0xF800

typedef struct _DXGK_DEBUG_REPORT_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
    DXGK_DEBUG_REPORT_HANDLE
    (*DbgReportCreate)(
        _In_ HANDLE DeviceHandle,
        _In_ ULONG Code,
        _In_ ULONG_PTR Arg1,
        _In_ ULONG_PTR Arg2,
        _In_ ULONG_PTR Arg3,
        _In_ ULONG_PTR Arg4);

    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
    _Success_(return != FALSE)
    BOOLEAN
    (*DbgReportSecondaryData)(
        _Inout_ DXGK_DEBUG_REPORT_HANDLE Report,
        _In_reads_bytes_(DataSize) PVOID Data,
        _In_ ULONG DataSize);

    _IRQL_requires_DXGK_(PASSIVE_LEVEL)
    VOID
    (*DbgReportComplete)(
        _Inout_ DXGK_DEBUG_REPORT_HANDLE Report);
} DXGK_DEBUG_REPORT_INTERFACE, *PDXGK_DEBUG_REPORT_INTERFACE;

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
#define DXGK_WHICHSPACE_CONFIG   PCI_WHICHSPACE_CONFIG
#define DXGK_WHICHSPACE_ROM      PCI_WHICHSPACE_ROM
#define DXGK_WHICHSPACE_MCH      0x80000000
#define DXGK_WHICHSPACE_BRIDGE   0x80000001
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
typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_GETNUMPATHS)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _Out_ SIZE_T *pNumPaths);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_GETNUMPATHSFROMSOURCE)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ SIZE_T *pNumPathsFromSource);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_ENUMPATHTARGETSFROMSOURCE)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_INDEX PathIndex,
    _Out_ D3DDDI_VIDEO_PRESENT_TARGET_ID *pVidPnTargetId);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_GETPATHSOURCEFROMTARGET)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    _Out_ D3DDDI_VIDEO_PRESENT_SOURCE_ID *pVidPnSourceId);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_ACQUIREPATHINFO)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH **ppVidPnPresentPathInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_ACQUIREFIRSTPATHINFO)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH **ppFirstVidPnPresentPathInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_ACQUIRENEXTPATHINFO)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPathInfo,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH **ppNextVidPnPresentPathInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_UPDATEPATHSUPPORTINFO)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPathInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_RELEASEPATHINFO)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPathInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_CREATENEWPATHINFO)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _Out_ D3DKMDT_VIDPN_PRESENT_PATH **ppNewVidPnPresentPathInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_ADDPATH)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPath);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTOPOLOGY_REMOVEPATH)(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId);

typedef struct _DXGK_VIDPNTOPOLOGY_INTERFACE
{
    DXGKDDI_VIDPNTOPOLOGY_GETNUMPATHS pfnGetNumPaths;
    DXGKDDI_VIDPNTOPOLOGY_GETNUMPATHSFROMSOURCE pfnGetNumPathsFromSource;
    DXGKDDI_VIDPNTOPOLOGY_ENUMPATHTARGETSFROMSOURCE pfnEnumPathTargetsFromSource;
    DXGKDDI_VIDPNTOPOLOGY_GETPATHSOURCEFROMTARGET pfnGetPathSourceFromTarget;
    DXGKDDI_VIDPNTOPOLOGY_ACQUIREPATHINFO pfnAcquirePathInfo;
    DXGKDDI_VIDPNTOPOLOGY_ACQUIREFIRSTPATHINFO pfnAcquireFirstPathInfo;
    DXGKDDI_VIDPNTOPOLOGY_ACQUIRENEXTPATHINFO pfnAcquireNextPathInfo;
    DXGKDDI_VIDPNTOPOLOGY_UPDATEPATHSUPPORTINFO pfnUpdatePathSupportInfo;
    DXGKDDI_VIDPNTOPOLOGY_RELEASEPATHINFO pfnReleasePathInfo;
    DXGKDDI_VIDPNTOPOLOGY_CREATENEWPATHINFO pfnCreateNewPathInfo;
    DXGKDDI_VIDPNTOPOLOGY_ADDPATH pfnAddPath;
    DXGKDDI_VIDPNTOPOLOGY_REMOVEPATH pfnRemovePath;
} DXGK_VIDPNTOPOLOGY_INTERFACE;

/* =========================================================================
 * DXGK_VIDPNSOURCEMODESET_INTERFACE
 *
 * Function table for manipulating a VidPN source mode set.
 * =========================================================================
 */
typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNSOURCEMODESET_GETNUMMODES)(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
    _Out_ CONST SIZE_T *pNumModes);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNSOURCEMODESET_ACQUIREFIRSTMODEINFO)(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE **ppFirstVidPnSourceModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNSOURCEMODESET_ACQUIRENEXTMODEINFO)(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *pVidPnSourceModeInfo,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE **ppNextVidPnSourceModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNSOURCEMODESET_ACQUIREPINNEDMODEINFO)(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE **ppPinnedVidPnSourceModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNSOURCEMODESET_RELEASEMODEINFO)(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *pVidPnSourceModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNSOURCEMODESET_CREATENEWMODEINFO)(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
    _Out_ D3DKMDT_VIDPN_SOURCE_MODE **ppNewVidPnSourceModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNSOURCEMODESET_ADDMODE)(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
    _In_ D3DKMDT_VIDPN_SOURCE_MODE *pVidPnSourceModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNSOURCEMODESET_PINMODE)(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
    _In_ D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID VidPnSourceModeId);

typedef struct _DXGK_VIDPNSOURCEMODESET_INTERFACE
{
    DXGKDDI_VIDPNSOURCEMODESET_GETNUMMODES pfnGetNumModes;
    DXGKDDI_VIDPNSOURCEMODESET_ACQUIREFIRSTMODEINFO pfnAcquireFirstModeInfo;
    DXGKDDI_VIDPNSOURCEMODESET_ACQUIRENEXTMODEINFO pfnAcquireNextModeInfo;
    DXGKDDI_VIDPNSOURCEMODESET_ACQUIREPINNEDMODEINFO pfnAcquirePinnedModeInfo;
    DXGKDDI_VIDPNSOURCEMODESET_RELEASEMODEINFO pfnReleaseModeInfo;
    DXGKDDI_VIDPNSOURCEMODESET_CREATENEWMODEINFO pfnCreateNewModeInfo;
    DXGKDDI_VIDPNSOURCEMODESET_ADDMODE pfnAddMode;
    DXGKDDI_VIDPNSOURCEMODESET_PINMODE pfnPinMode;
} DXGK_VIDPNSOURCEMODESET_INTERFACE;

/* =========================================================================
 * DXGK_VIDPNTARGETMODESET_INTERFACE
 *
 * Function table for manipulating a VidPN target mode set.
 * =========================================================================
 */
typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTARGETMODESET_GETNUMMODES)(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
    _Out_ CONST SIZE_T *pNumModes);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTARGETMODESET_ACQUIREFIRSTMODEINFO)(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE **ppFirstVidPnTargetModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTARGETMODESET_ACQUIRENEXTMODEINFO)(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *pVidPnTargetModeInfo,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE **ppNextVidPnTargetModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTARGETMODESET_ACQUIREPINNEDMODEINFO)(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE **ppPinnedVidPnTargetModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTARGETMODESET_RELEASEMODEINFO)(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *pVidPnTargetModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTARGETMODESET_CREATENEWMODEINFO)(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
    _Out_ D3DKMDT_VIDPN_TARGET_MODE **ppNewVidPnTargetModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTARGETMODESET_ADDMODE)(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
    _In_ D3DKMDT_VIDPN_TARGET_MODE *pVidPnTargetModeInfo);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPNTARGETMODESET_PINMODE)(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
    _In_ D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID VidPnTargetModeId);

typedef struct _DXGK_VIDPNTARGETMODESET_INTERFACE
{
    DXGKDDI_VIDPNTARGETMODESET_GETNUMMODES pfnGetNumModes;
    DXGKDDI_VIDPNTARGETMODESET_ACQUIREFIRSTMODEINFO pfnAcquireFirstModeInfo;
    DXGKDDI_VIDPNTARGETMODESET_ACQUIRENEXTMODEINFO pfnAcquireNextModeInfo;
    DXGKDDI_VIDPNTARGETMODESET_ACQUIREPINNEDMODEINFO pfnAcquirePinnedModeInfo;
    DXGKDDI_VIDPNTARGETMODESET_RELEASEMODEINFO pfnReleaseModeInfo;
    DXGKDDI_VIDPNTARGETMODESET_CREATENEWMODEINFO pfnCreateNewModeInfo;
    DXGKDDI_VIDPNTARGETMODESET_ADDMODE pfnAddMode;
    DXGKDDI_VIDPNTARGETMODESET_PINMODE pfnPinMode;
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
typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_GETTOPOLOGY)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _Out_ D3DKMDT_HVIDPNTOPOLOGY *phVidPnTopology,
    _Out_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE **ppVidPnTopologyInterface);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_ACQUIRESOURCEMODESET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET *phVidPnSourceModeSet,
    _Out_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE **ppVidPnSourceModeSetInterface);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_RELEASESOURCEMODESET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_CREATENEWSOURCEMODESET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET *phVidPnSourceModeSet,
    _Out_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE **ppVidPnSourceModeSetInterface);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_ASSIGNSOURCEMODESET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_ASSIGNMULTISAMPLINGMETHODSET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ CONST D3DDDI_MULTISAMPLINGMETHOD *pMultisamplingMethod);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_ACQUIRETARGETMODESET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET *phVidPnTargetModeSet,
    _Out_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE **ppVidPnTargetModeSetInterface);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_RELEASETARGETMODESET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_CREATENEWTARGETMODESET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET *phVidPnTargetModeSet,
    _Out_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE **ppVidPnTargetModeSetInterface);

typedef NTSTATUS (APIENTRY *DXGKDDI_VIDPN_ASSIGNTARGETMODESET)(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    _In_ D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet);

typedef struct _DXGK_VIDPN_INTERFACE
{
    DXGK_VIDPN_INTERFACE_VERSION Version;
    DXGKDDI_VIDPN_GETTOPOLOGY pfnGetTopology;
    DXGKDDI_VIDPN_ACQUIRESOURCEMODESET pfnAcquireSourceModeSet;
    DXGKDDI_VIDPN_RELEASESOURCEMODESET pfnReleaseSourceModeSet;
    DXGKDDI_VIDPN_CREATENEWSOURCEMODESET pfnCreateNewSourceModeSet;
    DXGKDDI_VIDPN_ASSIGNSOURCEMODESET pfnAssignSourceModeSet;
    DXGKDDI_VIDPN_ASSIGNMULTISAMPLINGMETHODSET pfnAssignMultisamplingMethodSet;
    DXGKDDI_VIDPN_ACQUIRETARGETMODESET pfnAcquireTargetModeSet;
    DXGKDDI_VIDPN_RELEASETARGETMODESET pfnReleaseTargetModeSet;
    DXGKDDI_VIDPN_CREATENEWTARGETMODESET pfnCreateNewTargetModeSet;
    DXGKDDI_VIDPN_ASSIGNTARGETMODESET pfnAssignTargetModeSet;
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

typedef struct _DXGK_INTERFACESPECIFICDATA
{
    HANDLE hAdapter;
    DXGKCB_GETHANDLEDATA pfnGetHandleDataCb;
    DXGKCB_GETHANDLEPARENT pfnGetHandleParentCb;
    DXGKCB_ENUMHANDLECHILDREN pfnEnumHandleChildrenCb;
    DXGKCB_NOTIFY_INTERRUPT pfnNotifyInterruptCb;
    DXGKCB_NOTIFY_DPC pfnNotifyDpcCb;
    DXGKCB_QUERYVIDPNINTERFACE pfnQueryVidPnInterfaceCb;
    DXGKCB_GETCAPTUREADDRESS pfnGetCaptureAddressCb;
} DXGK_INTERFACESPECIFICDATA;

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
typedef struct _DXGKRNL_INTERFACE
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
} DXGKRNL_INTERFACE, *PDXGKRNL_INTERFACE;

typedef DXGKRNL_INTERFACE DXGK_INTERFACE;
typedef PDXGKRNL_INTERFACE PDXGK_INTERFACE;

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

/*
 * SAL-decorated aliases used by the public WDK function-type declarations.
 * They intentionally carry no runtime state; keeping the canonical names lets
 * unmodified display miniports compile against the ReactOS DDK.
 */
typedef _In_ CONST PDEVICE_OBJECT IN_CONST_PDEVICE_OBJECT;
typedef _Inout_ PLINKED_DEVICE INOUT_PLINKED_DEVICE;
typedef _Inout_ PDXGK_CHILD_DESCRIPTOR INOUT_PDXGK_CHILD_DESCRIPTOR;
typedef _In_ PDXGK_CHILD_STATUS IN_PDXGK_CHILD_STATUS;
typedef _Inout_ PDXGK_CHILD_STATUS INOUT_PDXGK_CHILD_STATUS;
typedef _Inout_ PDXGK_DEVICE_DESCRIPTOR INOUT_PDXGK_DEVICE_DESCRIPTOR;
typedef _In_ DXGK_EVENT_TYPE IN_DXGK_EVENT_TYPE;
typedef _In_ PDXGK_START_INFO IN_PDXGK_START_INFO;
typedef _In_ PDXGKRNL_INTERFACE IN_PDXGKRNL_INTERFACE;
typedef _In_ PQUERY_INTERFACE IN_PQUERY_INTERFACE;
typedef _In_ PVIDEO_REQUEST_PACKET IN_PVIDEO_REQUEST_PACKET;

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

typedef NTSTATUS APIENTRY DXGKDDI_ADD_DEVICE(
    _In_  PDEVICE_OBJECT    PhysicalDeviceObject,
    _Out_ PVOID            *MiniportDeviceContext);

typedef NTSTATUS APIENTRY DXGKDDI_START_DEVICE(
    _In_  PVOID             MiniportDeviceContext,
    _In_  PDXGK_START_INFO  DxgkStartInfo,
    _In_  PDXGK_INTERFACE   DxgkInterface,
    _Out_ PULONG            NumberOfVideoPresentSources,
    _Out_ PULONG            NumberOfChildren);

typedef NTSTATUS APIENTRY DXGKDDI_STOP_DEVICE(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS APIENTRY DXGKDDI_REMOVE_DEVICE(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS APIENTRY DXGKDDI_DISPATCH_IO_REQUEST(
    _In_ PVOID                  MiniportDeviceContext,
    _In_ ULONG                  VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET  VideoRequestPacket);

typedef BOOLEAN APIENTRY DXGKDDI_INTERRUPT_ROUTINE(
    _In_ PVOID  MiniportDeviceContext,
    _In_ ULONG  MessageNumber);

typedef VOID APIENTRY DXGKDDI_DPC_ROUTINE(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS APIENTRY DXGKDDI_QUERY_CHILD_RELATIONS(
    _In_  PVOID                     MiniportDeviceContext,
    _Out_ PDXGK_CHILD_DESCRIPTOR    ChildRelations,
    _In_  ULONG                     ChildRelationsSize);

typedef NTSTATUS APIENTRY DXGKDDI_QUERY_CHILD_STATUS(
    _In_    PVOID               MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS  ChildStatus,
    _In_    BOOLEAN             NonDestructiveOnly);

typedef NTSTATUS APIENTRY DXGKDDI_QUERY_DEVICE_DESCRIPTOR(
    _In_    PVOID                       MiniportDeviceContext,
    _In_    ULONG                       ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR     DeviceDescriptor);

typedef NTSTATUS APIENTRY DXGKDDI_SET_POWER_STATE(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType);

typedef NTSTATUS APIENTRY DXGKDDI_NOTIFY_ACPI_EVENT(
    _In_      PVOID             MiniportDeviceContext,
    _In_      DXGK_EVENT_TYPE   EventType,
    _In_      ULONG             Event,
    _In_      PVOID             Argument,
    _Out_opt_ PULONG            AcpiFlags);

typedef VOID APIENTRY DXGKDDI_RESET_DEVICE(
    _In_ PVOID MiniportDeviceContext);

typedef VOID APIENTRY DXGKDDI_UNLOAD(VOID);

typedef NTSTATUS APIENTRY DXGKDDI_QUERY_INTERFACE(
    _In_ PVOID              MiniportDeviceContext,
    _In_ PQUERY_INTERFACE   QueryInterface);

typedef VOID APIENTRY DXGKDDI_CONTROL_ETW_LOGGING(
    _In_ BOOLEAN    Enable,
    _In_ ULONG      Flags,
    _In_ UCHAR      Level);

typedef DXGKDDI_CONTROL_ETW_LOGGING *PDXGKDDI_CONTROL_ETW_LOGGING;
typedef DXGKDDI_ADD_DEVICE *PDXGKDDI_ADD_DEVICE;
typedef DXGKDDI_START_DEVICE *PDXGKDDI_START_DEVICE;
typedef DXGKDDI_STOP_DEVICE *PDXGKDDI_STOP_DEVICE;
typedef DXGKDDI_REMOVE_DEVICE *PDXGKDDI_REMOVE_DEVICE;
typedef DXGKDDI_DISPATCH_IO_REQUEST *PDXGKDDI_DISPATCH_IO_REQUEST;
typedef DXGKDDI_INTERRUPT_ROUTINE *PDXGKDDI_INTERRUPT_ROUTINE;
typedef DXGKDDI_DPC_ROUTINE *PDXGKDDI_DPC_ROUTINE;
typedef DXGKDDI_QUERY_CHILD_RELATIONS *PDXGKDDI_QUERY_CHILD_RELATIONS;
typedef DXGKDDI_QUERY_CHILD_STATUS *PDXGKDDI_QUERY_CHILD_STATUS;
typedef DXGKDDI_QUERY_DEVICE_DESCRIPTOR *PDXGKDDI_QUERY_DEVICE_DESCRIPTOR;
typedef DXGKDDI_SET_POWER_STATE *PDXGKDDI_SET_POWER_STATE;
typedef DXGKDDI_NOTIFY_ACPI_EVENT *PDXGKDDI_NOTIFY_ACPI_EVENT;
typedef DXGKDDI_RESET_DEVICE *PDXGKDDI_RESET_DEVICE;
typedef DXGKDDI_UNLOAD *PDXGKDDI_UNLOAD;
typedef DXGKDDI_QUERY_INTERFACE *PDXGKDDI_QUERY_INTERFACE;

/* ---- Adapter information / capabilities -------------------------------- */

typedef PDXGKDDI_QUERYADAPTERINFO PDXGKDDI_QUERY_ADAPTER_INFO;

/* ---- Device / allocation management ------------------------------------ */

typedef PDXGKDDI_CREATEDEVICE PDXGKDDI_CREATE_DEVICE;
typedef PDXGKDDI_CREATEALLOCATION PDXGKDDI_CREATE_ALLOCATION;
typedef PDXGKDDI_DESTROYALLOCATION PDXGKDDI_DESTROY_ALLOCATION;
typedef PDXGKDDI_DESCRIBEALLOCATION PDXGKDDI_DESCRIBE_ALLOCATION;
typedef PDXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA PDXGKDDI_GET_STDALLOC_UPDATEFLAGS;
typedef PDXGKDDI_ACQUIRESWIZZLINGRANGE PDXGKDDI_ACQUIRE_SWIZZLING_RANGE;
typedef PDXGKDDI_RELEASESWIZZLINGRANGE PDXGKDDI_RELEASE_SWIZZLING_RANGE;

/* ---- DMA command buffer submission ------------------------------------- */

typedef PDXGKDDI_SUBMITCOMMAND PDXGKDDI_SUBMIT_COMMAND;
typedef PDXGKDDI_PREEMPTCOMMAND PDXGKDDI_PREEMPT_COMMAND;
typedef PDXGKDDI_BUILDPAGINGBUFFER PDXGKDDI_BUILD_PAGING_BUFFER;

/* ---- Palette / pointer / present --------------------------------------- */

typedef PDXGKDDI_SETPALETTE PDXGKDDI_SET_PALETTE;
typedef PDXGKDDI_SETPOINTERPOSITION PDXGKDDI_SET_POINTER_POSITION;
typedef PDXGKDDI_SETPOINTERSHAPE PDXGKDDI_SET_POINTER_SHAPE;

/* ---- TDR (Timeout Detection and Recovery) ------------------------------ */

/* ---- Escape / debug ---------------------------------------------------- */

typedef PDXGKDDI_COLLECTDBGINFO PDXGKDDI_COLLECT_DB_ENGINE_INFO;
typedef PDXGKDDI_QUERYCURRENTFENCE PDXGKDDI_QUERY_CURRENT_FENCE;

/* ---- VidPN management -------------------------------------------------- */

typedef PDXGKDDI_ISSUPPORTEDVIDPN PDXGKDDI_IS_SUPPORTED_VIDPN;
typedef PDXGKDDI_RECOMMENDFUNCTIONALVIDPN PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN;
typedef PDXGKDDI_ENUMVIDPNCOFUNCMODALITY PDXGKDDI_ENUM_VIDPN_COFUNC_MODALITY;
typedef PDXGKDDI_SETVIDPNSOURCEADDRESS PDXGKDDI_SET_VIDPN_SOURCE_ADDRESS;
typedef PDXGKDDI_SETVIDPNSOURCEVISIBILITY PDXGKDDI_SET_VIDPN_SOURCE_VISIBILITY;
typedef PDXGKDDI_COMMITVIDPN PDXGKDDI_COMMIT_VIDPN;
typedef PDXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH;
typedef PDXGKDDI_RECOMMENDMONITORMODES PDXGKDDI_RECOMMEND_MONITORMODES;
typedef PDXGKDDI_RECOMMENDVIDPNTOPOLOGY PDXGKDDI_RECOMMEND_VIDPN_TOPOLOGY;

/* ---- Scan-line / interrupt control ------------------------------------- */

typedef PDXGKDDI_GETSCANLINE PDXGKDDI_GET_SCAN_LINE;
typedef PDXGKDDI_STOPCAPTURE PDXGKDDI_STOP_CAPTURE;
typedef PDXGKDDI_CONTROLINTERRUPT PDXGKDDI_CONTROL_INTERRUPT;

/* ---- Overlay ----------------------------------------------------------- */

typedef PDXGKDDI_CREATEOVERLAY PDXGKDDI_CREATE_OVERLAY;

/* ---- Per-device/context/allocation callbacks (device-level DDIs) ------- */

typedef PDXGKDDI_DESTROYDEVICE PDXGKDDI_DESTROY_DEVICE;
typedef PDXGKDDI_OPENALLOCATIONINFO PDXGKDDI_OPEN_ALLOCATION;
typedef PDXGKDDI_CLOSEALLOCATION PDXGKDDI_CLOSE_ALLOCATION;
typedef PDXGKDDI_UPDATEOVERLAY PDXGKDDI_UPDATE_OVERLAY;
typedef PDXGKDDI_FLIPOVERLAY PDXGKDDI_FLIP_OVERLAY;
typedef PDXGKDDI_DESTROYOVERLAY PDXGKDDI_DESTROY_OVERLAY;
typedef PDXGKDDI_CREATECONTEXT PDXGKDDI_CREATE_CONTEXT;
typedef PDXGKDDI_DESTROYCONTEXT PDXGKDDI_DESTROY_CONTEXT;

/* ---- Multi-GPU linked adapter ----------------------------------------- */

typedef NTSTATUS APIENTRY DXGKDDI_LINK_DEVICE(
    _In_    CONST PDEVICE_OBJECT    PhysicalDeviceObject,
    _In_    CONST PVOID             MiniportDeviceContext,
    _Inout_ PLINKED_DEVICE          LinkedDevice);

typedef DXGKDDI_LINK_DEVICE *PDXGKDDI_LINK_DEVICE;

/* ---- Private display driver format ------------------------------------- */

typedef PDXGKDDI_SETDISPLAYPRIVATEDRIVERFORMAT PDXGKDDI_SET_DISPLAY_PRIVATE_DRIVER_FORMAT;

/* ---- VidPN hardware capabilities ------------------------------------- */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
typedef PDXGKDDI_QUERYVIDPNHWCAPABILITY PDXGKDDI_QUERY_VIDPN_HW_CAPABILITY;
#endif

/* ---- Win8 per-engine TDR callbacks ----------------------------------- */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
typedef PDXGKDDI_QUERYENGINESTATUS PDXGKDDI_QUERY_ENGINE_STATUS;
typedef PDXGKDDI_RESETENGINE PDXGKDDI_RESET_ENGINE;
#endif

typedef NTSTATUS APIENTRY DXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo);

typedef DXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP
    *PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP;

typedef struct _DXGK_CHILD_CONTAINER_ID
{
    GUID ContainerId;
    struct
    {
        ULONG64 PortId;
        USHORT ManufacturerName;
        USHORT ProductCode;
    } EldInfo;
} DXGK_CHILD_CONTAINER_ID, *PDXGK_CHILD_CONTAINER_ID;

typedef NTSTATUS APIENTRY DXGKDDI_GET_CHILD_CONTAINER_ID(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_CHILD_CONTAINER_ID ContainerId);

typedef DXGKDDI_GET_CHILD_CONTAINER_ID *PDXGKDDI_GET_CHILD_CONTAINER_ID;

typedef NTSTATUS APIENTRY DXGKDDI_SYSTEM_DISPLAY_ENABLE(
    _In_  PVOID                              MiniportDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID     TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ PUINT                              Width,
    _Out_ PUINT                              Height,
    _Out_ D3DDDIFORMAT                      *ColorFormat);

typedef VOID APIENTRY DXGKDDI_SYSTEM_DISPLAY_WRITE(
    _In_ PVOID  MiniportDeviceContext,
    _In_ PVOID  Source,
    _In_ UINT   SourceWidth,
    _In_ UINT   SourceHeight,
    _In_ UINT   SourceStride,
    _In_ UINT   PositionX,
    _In_ UINT   PositionY);

typedef DXGKDDI_SYSTEM_DISPLAY_ENABLE *PDXGKDDI_SYSTEM_DISPLAY_ENABLE;
typedef DXGKDDI_SYSTEM_DISPLAY_WRITE *PDXGKDDI_SYSTEM_DISPLAY_WRITE;

typedef PDXGKDDI_CANCELCOMMAND PDXGKDDI_CANCEL_COMMAND;

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
    PDXGKDDISETPOWERCOMPONENTFSTATE             DxgkDdiSetPowerComponentFState;
    PDXGKDDI_QUERYDEPENDENTENGINEGROUP           DxgkDdiQueryDependentEngineGroup;
    PDXGKDDI_QUERY_ENGINE_STATUS                DxgkDdiQueryEngineStatus;
    PDXGKDDI_RESET_ENGINE                       DxgkDdiResetEngine;
    PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE              DxgkDdiSystemDisplayEnable;
    PDXGKDDI_SYSTEM_DISPLAY_WRITE               DxgkDdiSystemDisplayWrite;
    PDXGKDDI_CANCEL_COMMAND                     DxgkDdiCancelCommand;
    PDXGKDDI_GET_CHILD_CONTAINER_ID              DxgkDdiGetChildContainerId;
    PDXGKDDIPOWERRUNTIMECONTROLREQUEST           DxgkDdiPowerRuntimeControlRequest;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay;
    PDXGKDDI_NOTIFY_SURPRISE_REMOVAL            DxgkDdiNotifySurpriseRemoval;
#endif

    /* ---- WDDM 1.3 / Win8.1 additions ------------------------------------ */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    PDXGKDDI_GETNODEMETADATA                    DxgkDdiGetNodeMetadata;
    PDXGKDDISETPOWERPSTATE                      DxgkDdiSetPowerPState;
    PDXGKDDI_CONTROLINTERRUPT2                   DxgkDdiControlInterrupt2;
    PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT      DxgkDdiCheckMultiPlaneOverlaySupport;
    PDXGKDDI_CALIBRATEGPUCLOCK                  DxgkDdiCalibrateGpuClock;
    PDXGKDDI_FORMATHISTORYBUFFER                DxgkDdiFormatHistoryBuffer;
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
    PDXGKDDI_PRESENTDISPLAYONLY                 DxgkDdiPresentDisplayOnly;
    PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE              DxgkDdiSystemDisplayEnable;
    PDXGKDDI_SYSTEM_DISPLAY_WRITE               DxgkDdiSystemDisplayWrite;
    PDXGKDDI_GET_CHILD_CONTAINER_ID              DxgkDdiGetChildContainerId;
    PDXGKDDI_CONTROL_INTERRUPT                  DxgkDdiControlInterrupt;
    PDXGKDDISETPOWERCOMPONENTFSTATE             DxgkDdiSetPowerComponentFState;
    PDXGKDDIPOWERRUNTIMECONTROLREQUEST           DxgkDdiPowerRuntimeControlRequest;
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


#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)

DEFINE_GUID(GUID_DXGKDDI_GPU_PARTITION_INTERFACE,
            0x462bc153, 0x40eb, 0x484a, 0x81, 0x68, 0x99, 0x72, 0xe3, 0xcd, 0x5a, 0xef);

#define DXGK_VIRTUALIZED_ACS_CAPABLE 0x80
#define DXGK_VIRTUALIZED_UNIQUE_RID 0x100
#define DXGK_VIRTUALIZED_PARAVIRTUALIZED 0x400
#define DXGK_VIRTUALIZED_HOST_VIRTUAL_DEVICE 0x800

typedef struct _DXGKARG_GETGPUPARTITIONINFO
{
    ULONG NumGpuPartitionOptions;
    USHORT *pGpuPartitionOptions;
    USHORT CurrentGpuPartitionCount;
} DXGKARG_GETGPUPARTITIONINFO, *PDXGKARG_GETGPUPARTITIONINFO;

typedef NTSTATUS DXGKDDI_GETGPUPARTITIONINFO(
    HANDLE Context,
    DXGKARG_GETGPUPARTITIONINFO *pArgs);
typedef DXGKDDI_GETGPUPARTITIONINFO *PDXGKDDI_GETGPUPARTITIONINFO;

typedef enum _DXGK_VGPU_CAPABILITY_TYPE
{
    DXGK_VGPU_CAPABILITY_MEMORY = 0,
    DXGK_VGPU_CAPABILITY_ENCODE = 1,
    DXGK_VGPU_CAPABILITY_DECODE = 2,
    DXGK_VGPU_CAPABILITY_COMPUTE = 3,
    DXGK_VGPU_CAPABILITY_MAX,
} DXGK_VGPU_CAPABILITY_TYPE;

typedef struct _DXGK_VGPU_PROFILE_CAPABILITY
{
    UINT64 TotalValue;
    UINT64 AvailableValue;
    UINT64 MinPartitionValue;
    UINT64 MaxPartitionValue;
    UINT64 OptimalPartitionValue;
} DXGK_VGPU_PROFILE_CAPABILITY, *PDXGK_VGPU_PROFILE_CAPABILITY;

typedef struct _DXGKARG_GETVIRTUALGPUPROFILE
{
    ULONG PartitionCount;
    ULONG VirtualizationFlags;
    DXGK_VGPU_PROFILE_CAPABILITY ProfileCapability[DXGK_VGPU_CAPABILITY_MAX];
} DXGKARG_GETVIRTUALGPUPROFILE, *PDXGKARG_GETVIRTUALGPUPROFILE;

typedef NTSTATUS DXGKDDI_GETVIRTUALGPUPROFILE(
    HANDLE Context,
    DXGKARG_GETVIRTUALGPUPROFILE *pArgs);
typedef DXGKDDI_GETVIRTUALGPUPROFILE *PDXGKDDI_GETVIRTUALGPUPROFILE;

typedef struct _DXGK_GPUENGINE
{
    DXGK_ENGINE_TYPE EngineType;
    WCHAR Name[DXGK_MAX_METADATA_NAME_LENGTH];
    ULONG NumPartitionUnits;
} DXGK_GPUENGINE, *PDXGK_GPUENGINE;

typedef struct _DXGKARG_GETGPUENGINES
{
    ULONG NumEngines;
    DXGK_GPUENGINE EngineInfo[1];
} DXGKARG_GETGPUENGINES, *PDXGKARG_GETGPUENGINES;

typedef NTSTATUS DXGKDDI_GETGPUENGINES(
    HANDLE Context,
    DXGKARG_GETGPUENGINES *pArgs);
typedef DXGKDDI_GETGPUENGINES *PDXGKDDI_GETGPUENGINES;

typedef struct _DXGKARG_SETGPUPARTITIONCOUNT
{
    ULONG PartitionCount;
} DXGKARG_SETGPUPARTITIONCOUNT, *PDXGKARG_SETGPUPARTITIONCOUNT;

typedef NTSTATUS DXGKDDI_SETGPUPARTITIONCOUNT(
    HANDLE Context,
    DXGKARG_SETGPUPARTITIONCOUNT *pArgs);
typedef DXGKDDI_SETGPUPARTITIONCOUNT *PDXGKDDI_SETGPUPARTITIONCOUNT;

typedef struct _DXGK_VIRTUALGPUCAPABILITY
{
    UINT64 MinValue;
    UINT64 MaxValue;
    UINT64 OptimalValue;
} DXGK_VIRTUALGPUCAPABILITY, *PDXGK_VIRTUALGPUCAPABILITY;

typedef struct _DXGK_VIRTUALGPUPROFILE
{
    DXGK_VIRTUALGPUCAPABILITY Capability[DXGK_VGPU_CAPABILITY_MAX];
} DXGK_VIRTUALGPUPROFILE, *PDXGK_VIRTUALGPUPROFILE;

typedef struct _DXGK_VIRTUALGPUENGINEINFO
{
    ULONG MinPartitionUnits;
    ULONG MaxPartitionUnits;
    ULONG EngineId;
} DXGK_VIRTUALGPUENGINEINFO, *PDXGK_VIRTUALGPUENGINEINFO;

typedef struct _DXGK_VIRTUALGPUSEGMENTINFO
{
    ULONG DriverSegmentId;
    UINT64 Size;
    UINT Alignment;
    UINT64 MinSegmentOffset;
    UINT64 MaxSegmentOffset;
    UINT PrivateDriverData;
} DXGK_VIRTUALGPUSEGMENTINFO, *PDXGK_VIRTUALGPUSEGMENTINFO;

#define DXGK_MAX_VIRTUAL_GPU_ALLOCATIONS 32

typedef struct _DXGKARG_CREATEVIRTUALGPU
{
    ULONG PartitionId;
    DXGK_VIRTUALGPUPROFILE Profile;
    CLSID UserModeVirtualDeviceProvider;
    LUID VirtualGpuLuid;
    ULONG NumMemorySegments;
    DXGK_VIRTUALGPUSEGMENTINFO SegmentInfo[DXGK_MAX_VIRTUAL_GPU_ALLOCATIONS];
    ULONG NumEngines;
    DXGK_VIRTUALGPUENGINEINFO EngineInfo[DXGK_MAX_ASYMETRICAL_PROCESSING_NODES];
} DXGKARG_CREATEVIRTUALGPU, *PDXGKARG_CREATEVIRTUALGPU;

typedef NTSTATUS DXGKDDI_CREATEVIRTUALGPU(
    HANDLE Context,
    DXGKARG_CREATEVIRTUALGPU *pArgs);
typedef DXGKDDI_CREATEVIRTUALGPU *PDXGKDDI_CREATEVIRTUALGPU;

typedef struct _DXGK_VGPU_CAPABILITY
{
    UINT64 MinValue;
    UINT64 MaxValue;
    UINT64 CurrentValue;
} DXGK_VGPU_CAPABILITY, *PDXGK_VGPU_CAPABILITY;

typedef struct _DXGKARG_GETVIRTUALGPUINFO
{
    ULONG PartitionId;
    DXGK_VGPU_CAPABILITY Capability[DXGK_VGPU_CAPABILITY_MAX];
} DXGKARG_GETVIRTUALGPUINFO, *PDXGKARG_GETVIRTUALGPUINFO;

typedef NTSTATUS DXGKDDI_GETVIRTUALGPUINFO(
    HANDLE Context,
    DXGKARG_GETVIRTUALGPUINFO *pArgs);
typedef DXGKDDI_GETVIRTUALGPUINFO *PDXGKDDI_GETVIRTUALGPUINFO;

typedef struct _DXGKARG_SETVIRTUALGPUVMBUS
{
    ULONG VirtualGpuIndex;
    HANDLE VmBusHandle;
} DXGKARG_SETVIRTUALGPUVMBUS, *PDXGKARG_SETVIRTUALGPUVMBUS;

typedef NTSTATUS DXGKDDI_SETVIRTUALGPUVMBUS(
    HANDLE Context,
    DXGKARG_SETVIRTUALGPUVMBUS *pArgs);
typedef DXGKDDI_SETVIRTUALGPUVMBUS *PDXGKDDI_SETVIRTUALGPUVMBUS;

typedef struct _DXGK_GPU_PHYSICAL_ADDRESS
{
    ULONG MemorySegmentId;
    UINT64 MemorySegmentOffset;
} DXGK_GPU_PHYSICAL_ADDRESS, *PDXGK_GPU_PHYSICAL_ADDRESS;

typedef struct _DXGK_VIRTUALGPUMEMORYRESOURCE
{
    HANDLE DriverAllocationHandle;
    DXGK_GPU_PHYSICAL_ADDRESS AllocationAddress;
    UINT64 AllocationSize;
} DXGK_VIRTUALGPUMEMORYRESOURCE, *PDXGK_VIRTUALGPUMEMORYRESOURCE;

typedef struct _DXGKARG_SETVIRTUALGPURESOURCES
{
    ULONG PartitionId;
    ULONG NumMemoryAllocations;
    DXGK_VIRTUALGPUMEMORYRESOURCE MemoryInfo[1];
} DXGKARG_SETVIRTUALGPURESOURCES, *PDXGKARG_SETVIRTUALGPURESOURCES;

typedef NTSTATUS DXGKDDI_SETVIRTUALGPURESOURCES(
    HANDLE Context,
    DXGKARG_SETVIRTUALGPURESOURCES *pArgs);
typedef DXGKDDI_SETVIRTUALGPURESOURCES *PDXGKDDI_SETVIRTUALGPURESOURCES;

typedef struct _DXGKARG_DESTROYVIRTUALGPU
{
    ULONG PartitionId;
} DXGKARG_DESTROYVIRTUALGPU, *PDXGKARG_DESTROYVIRTUALGPU;

typedef NTSTATUS DXGKDDI_DESTROYVIRTUALGPU(
    HANDLE Context,
    DXGKARG_DESTROYVIRTUALGPU *pArgs);
typedef DXGKDDI_DESTROYVIRTUALGPU *PDXGKDDI_DESTROYVIRTUALGPU;

typedef struct _DXGKARG_SUSPENDVIRTUALGPU
{
    ULONG PartitionId;
} DXGKARG_SUSPENDVIRTUALGPU, *PDXGKARG_SUSPENDVIRTUALGPU;

typedef NTSTATUS DXGKDDI_SUSPENDVIRTUALGPU(
    HANDLE Context,
    DXGKARG_SUSPENDVIRTUALGPU *pArgs);
typedef DXGKDDI_SUSPENDVIRTUALGPU *PDXGKDDI_SUSPENDVIRTUALGPU;

typedef struct _DXGKARG_RESUMEVIRTUALGPU
{
    ULONG PartitionId;
} DXGKARG_RESUMEVIRTUALGPU, *PDXGKARG_RESUMEVIRTUALGPU;

typedef NTSTATUS DXGKDDI_RESUMEVIRTUALGPU(
    HANDLE Context,
    DXGKARG_RESUMEVIRTUALGPU *pArgs);
typedef DXGKDDI_RESUMEVIRTUALGPU *PDXGKDDI_RESUMEVIRTUALGPU;

typedef struct _DXGK_VIRTUALGPUDRIVERESCAPE
{
    ULONG PartitionId;
    ULONG InputBufferSize;
    ULONG OutputBufferSize;
    PVOID pInputBuffer;
    PVOID pOutputBuffer;
} DXGK_VIRTUALGPUDRIVERESCAPE, *PDXGK_VIRTUALGPUDRIVERESCAPE;

typedef NTSTATUS DXGKDDI_VIRTUALGPUDRIVERESCAPE(
    HANDLE Context,
    PDXGK_VIRTUALGPUDRIVERESCAPE pArgs);
typedef DXGKDDI_VIRTUALGPUDRIVERESCAPE *PDXGKDDI_VIRTUALGPUDRIVERESCAPE;

typedef struct _DXGKDDI_GPU_PARTITION_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDXGKDDI_GETGPUPARTITIONINFO DxgkDdiGetGpuPartitionInfo;
    PDXGKDDI_SETGPUPARTITIONCOUNT DxgkDdiSetGpuPartitionCount;
    PDXGKDDI_GETGPUENGINES DxgkDdiGetGpuEngines;
    PDXGKDDI_GETVIRTUALGPUPROFILE DxgkDdiGetVirtualGpuProfile;
    PDXGKDDI_CREATEVIRTUALGPU DxgkDdiCreateVirtualGpu;
    PDXGKDDI_GETVIRTUALGPUINFO DxgkDdiGetVirtualGpuInfo;
    PDXGKDDI_SETVIRTUALGPURESOURCES DxgkDdiSetVirtualGpuResources;
    PDXGKDDI_DESTROYVIRTUALGPU DxgkDdiDestroyVirtualGpu;
    PDXGKDDI_SUSPENDVIRTUALGPU DxgkDdiSuspendVirtualGpu;
    PDXGKDDI_RESUMEVIRTUALGPU DxgkDdiResumeVirtualGpu;
    PDXGKDDI_VIRTUALGPUDRIVERESCAPE DxgkDdiVirtualGpuDriverEscape;
    PDXGKDDI_SETVIRTUALGPUVMBUS DxgkDdiSetVirtualGpuVmBus;
} DXGKDDI_GPU_PARTITION_INTERFACE, *PDXGKDDI_GPU_PARTITION_INTERFACE;

#define DXGKDDI_GPU_PARTITION_INTERFACE_VERSION 1

DEFINE_GUID(GUID_DXGKDDI_SRIOV_INTERFACE,
            0xd7172248, 0xf296, 0x4c50, 0xa6, 0xf5, 0xd2, 0x5d, 0x80, 0x73, 0x06, 0x3a);

typedef struct _DXGKARG_READVIRTUALFUNCTIONCONFIG
{
    PVOID Data;
    ULONG VirtualFunctionIndex;
    ULONG Offset;
    ULONG Length;
} DXGKARG_READVIRTUALFUNCTIONCONFIG, *PDXGKARG_READVIRTUALFUNCTIONCONFIG;

typedef NTSTATUS DXGKDDI_READVIRTUALFUNCTIONCONFIG(
    HANDLE Context,
    DXGKARG_READVIRTUALFUNCTIONCONFIG *pArgs);
typedef DXGKDDI_READVIRTUALFUNCTIONCONFIG *PDXGKDDI_READVIRTUALFUNCTIONCONFIG;

typedef struct _DXGKARG_WRITEVIRTUALFUNCTIONCONFIG
{
    PVOID Data;
    ULONG VirtualFunctionIndex;
    ULONG Offset;
    ULONG Length;
} DXGKARG_WRITEVIRTUALFUNCTIONCONFIG, *PDXGKARG_WRITEVIRTUALFUNCTIONCONFIG;

typedef NTSTATUS DXGKDDI_WRITEVIRTUALFUNCTIONCONFIG(
    HANDLE Context,
    DXGKARG_WRITEVIRTUALFUNCTIONCONFIG *pArgs);
typedef DXGKDDI_WRITEVIRTUALFUNCTIONCONFIG *PDXGKDDI_WRITEVIRTUALFUNCTIONCONFIG;

typedef struct _DXGKARG_READVIRTUALFUNCTIONCONFIGBLOCK
{
    PVOID Data;
    ULONG VirtualFunctionIndex;
    ULONG BlockId;
    ULONG Length;
} DXGKARG_READVIRTUALFUNCTIONCONFIGBLOCK, *PDXGKARG_READVIRTUALFUNCTIONCONFIGBLOCK;

typedef NTSTATUS DXGKDDI_READVIRTUALFUNCTIONCONFIGBLOCK(
    HANDLE Context,
    DXGKARG_READVIRTUALFUNCTIONCONFIGBLOCK *pArgs);
typedef DXGKDDI_READVIRTUALFUNCTIONCONFIGBLOCK *PDXGKDDI_READVIRTUALFUNCTIONCONFIGBLOCK;

typedef struct _DXGKARG_WRITEVIRTUALFUNCTIONCONFIGBLOCK
{
    PVOID Data;
    ULONG VirtualFunctionIndex;
    ULONG BlockId;
    ULONG Length;
} DXGKARG_WRITEVIRTUALFUNCTIONCONFIGBLOCK, *PDXGKARG_WRITEVIRTUALFUNCTIONCONFIGBLOCK;

typedef NTSTATUS DXGKDDI_WRITEVIRTUALFUNCTIONCONFIGBLOCK(
    HANDLE Context,
    DXGKARG_WRITEVIRTUALFUNCTIONCONFIGBLOCK *pArgs);
typedef DXGKDDI_WRITEVIRTUALFUNCTIONCONFIGBLOCK *PDXGKDDI_WRITEVIRTUALFUNCTIONCONFIGBLOCK;

#define DXGK_MAX_NUM_PCI_BARS 6

typedef struct _DXGKARG_QUERYPROBEDBARS
{
    ULONG VirtualFunctionIndex;
    PULONG BaseRegisterValues;
} DXGKARG_QUERYPROBEDBARS, *PDXGKARG_QUERYPROBEDBARS;

typedef VOID DXGKDDI_QUERYPROBEDBARS(
    HANDLE Context,
    DXGKARG_QUERYPROBEDBARS *pArgs);
typedef DXGKDDI_QUERYPROBEDBARS *PDXGKDDI_QUERYPROBEDBARS;

typedef struct _DXGKARG_QUERYVIRTUALFUNCTIONLUID
{
    ULONG VirtualFunctionIndex;
    PLUID pLuid;
} DXGKARG_QUERYVIRTUALFUNCTIONLUID, *PDXGKARG_QUERYVIRTUALFUNCTIONLUID;

typedef VOID DXGKDDI_QUERYVIRTUALFUNCTIONLUID(
    HANDLE Context,
    DXGKARG_QUERYVIRTUALFUNCTIONLUID *pArgs);
typedef DXGKDDI_QUERYVIRTUALFUNCTIONLUID *PDXGKDDI_QUERYVIRTUALFUNCTIONLUID;

typedef struct _DXGKARG_GETVENDORANDDEVICE
{
    ULONG VirtualFunctionIndex;
    USHORT VendorId;
    USHORT DeviceId;
} DXGKARG_GETVENDORANDDEVICE, *PDXGKARG_GETVENDORANDDEVICE;

typedef VOID DXGKDDI_GETVENDORANDDEVICE(
    HANDLE Context,
    DXGKARG_GETVENDORANDDEVICE *pArgs);
typedef DXGKDDI_GETVENDORANDDEVICE *PDXGKDDI_GETVENDORANDDEVICE;

typedef struct _DXGKARG_GETDEVICELOCATION
{
    ULONG VirtualFunctionIndex;
    ULONG SegmentNumber;
    ULONG BusNumber;
    ULONG FunctionNumber;
} DXGKARG_GETDEVICELOCATION, *PDXGKARG_GETDEVICELOCATION;

typedef VOID DXGKDDI_GETDEVICELOCATION(
    HANDLE Context,
    DXGKARG_GETDEVICELOCATION *pArgs);
typedef DXGKDDI_GETDEVICELOCATION *PDXGKDDI_GETDEVICELOCATION;

typedef struct _DXGKARG_RESETVIRTUALFUNCTION
{
    ULONG VirtualFunctionIndex;
} DXGKARG_RESETVIRTUALFUNCTION, *PDXGKARG_RESETVIRTUALFUNCTION;

typedef NTSTATUS DXGKDDI_RESETVIRTUALFUNCTION(
    HANDLE Context,
    DXGKARG_RESETVIRTUALFUNCTION *pArgs);
typedef DXGKDDI_RESETVIRTUALFUNCTION *PDXGKDDI_RESETVIRTUALFUNCTION;

typedef struct _DXGKARG_SETVIRTUALFUNCTIONPOWERSTATE
{
    ULONG VirtualFunctionIndex;
    DEVICE_POWER_STATE PowerState;
    BOOLEAN Wake;
} DXGKARG_SETVIRTUALFUNCTIONPOWERSTATE, *PDXGKARG_SETVIRTUALFUNCTIONPOWERSTATE;

typedef NTSTATUS DXGKDDI_SETVIRTUALFUNCTIONPOWERSTATE(
    HANDLE Context,
    DXGKARG_SETVIRTUALFUNCTIONPOWERSTATE *pArgs);
typedef DXGKDDI_SETVIRTUALFUNCTIONPOWERSTATE *PDXGKDDI_SETVIRTUALFUNCTIONPOWERSTATE;

typedef struct _DXGKARG_GETRESOURCEFORBAR
{
    ULONG VirtualFunctionIndex;
    ULONG BarIndex;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pResource;
} DXGKARG_GETRESOURCEFORBAR, *PDXGKARG_GETRESOURCEFORBAR;

typedef NTSTATUS DXGKDDI_GETRESOURCEFORBAR(
    HANDLE Context,
    DXGKARG_GETRESOURCEFORBAR *pArgs);
typedef DXGKDDI_GETRESOURCEFORBAR *PDXGKDDI_GETRESOURCEFORBAR;

typedef struct _DXGKDDI_SRIOV_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDXGKDDI_READVIRTUALFUNCTIONCONFIG DxgkDdiReadVirtualFunctionConfig;
    PDXGKDDI_WRITEVIRTUALFUNCTIONCONFIG DxgkDdiWriteVirtualFunctionConfig;
    PDXGKDDI_READVIRTUALFUNCTIONCONFIGBLOCK DxgkDdiReadVirtualFunctionConfigBlock;
    PDXGKDDI_WRITEVIRTUALFUNCTIONCONFIGBLOCK DxgkDdiWriteVirtualFunctionConfigBlock;
    PDXGKDDI_QUERYPROBEDBARS DxgkDdiQueryProbedBars;
    PDXGKDDI_GETVENDORANDDEVICE DxgkDdiGetVendorAndDevice;
    PDXGKDDI_GETDEVICELOCATION DxgkDdiGetDeviceLocation;
    PDXGKDDI_RESETVIRTUALFUNCTION DxgkDdiResetVirtualFunction;
    PDXGKDDI_SETVIRTUALFUNCTIONPOWERSTATE DxgkDdiSetVirtualFunctionPowerState;
    PDXGKDDI_GETRESOURCEFORBAR DxgkDdiGetResourceForBar;
    PDXGKDDI_QUERYVIRTUALFUNCTIONLUID DxgkDdiQueryVirtualFunctionLuid;
} DXGKDDI_SRIOV_INTERFACE, *PDXGKDDI_SRIOV_INTERFACE;

#define DXGKDDI_SRIOV_INTERFACE_VERSION 1

DEFINE_GUID(GUID_DXGKDDI_MITIGABLE_DEVICE_INTERFACE,
            0x1387f270, 0x121a, 0x4a4a, 0xb2, 0x5e, 0x3b, 0x15, 0x89, 0x97, 0x6c, 0x61);

typedef struct _DXGKARG_QUERYMITIGATEDRANGECOUNT
{
    ULONG VirtualFunctionIndex;
    ULONG RangeCount[DXGK_MAX_NUM_PCI_BARS];
} DXGKARG_QUERYMITIGATEDRANGECOUNT, *PDXGKARG_QUERYMITIGATEDRANGECOUNT;

typedef VOID DXGKDDI_QUERYMITIGATEDRANGECOUNT(
    HANDLE Context,
    DXGKARG_QUERYMITIGATEDRANGECOUNT *pArgs);
typedef DXGKDDI_QUERYMITIGATEDRANGECOUNT *PDXGKDDI_QUERYMITIGATEDRANGECOUNT;

typedef struct _DXGK_MITIGATEDRANGEINFO
{
    ULONG64 BasePageNumber;
    ULONG PageCount;
    BOOLEAN InterceptReads;
    BOOLEAN InterceptWrites;
} DXGK_MITIGATEDRANGEINFO, *PDXGK_MITIGATEDRANGEINFO;

typedef struct _DXGKARG_QUERYMITIGATEDRANGES
{
    ULONG VirtualFunctionIndex;
    ULONG BarIndex;
    ULONG NumRanges;
    PDXGK_MITIGATEDRANGEINFO pMitigatedRange;
} DXGKARG_QUERYMITIGATEDRANGES, *PDXGKARG_QUERYMITIGATEDRANGES;

typedef NTSTATUS DXGKDDI_QUERYMITIGATEDRANGES(
    HANDLE Context,
    DXGKARG_QUERYMITIGATEDRANGES *pArgs);
typedef DXGKDDI_QUERYMITIGATEDRANGES *PDXGKDDI_QUERYMITIGATEDRANGES;

typedef struct _DXGKDDI_MITIGABLE_DEVICE_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDXGKDDI_QUERYMITIGATEDRANGECOUNT DxgkDdiQueryMitigatedRangeCount;
    PDXGKDDI_QUERYMITIGATEDRANGES DxgkDdiQueryMitigatedRanges;
} DXGKDDI_MITIGABLE_DEVICE_INTERFACE, *PDXGKDDI_MITIGABLE_DEVICE_INTERFACE;

#define DXGKDDI_MITIGABLE_DEVICE_INTERFACE_VERSION 1

DEFINE_GUID(GUID_DXGKDDI_FLEXIOV_DEVICE_INTERFACE,
            0x7b73a997, 0x48e8, 0x4cab, 0x9f, 0xab, 0xe0, 0x77, 0x4b, 0x44, 0xf5, 0x99);

#define DXGKDDI_MAX_FLEXIOV_RESOURCES 32

typedef struct _DXGKARG_GETBACKINGRESOURCE
{
    ULONG VirtualFunctionIndex;
    USHORT ResourceIndex;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;
    PMDL pMdl;
} DXGKARG_GETBACKINGRESOURCE, *PDXGKARG_GETBACKINGRESOURCE;

typedef NTSTATUS DXGKDDI_GETBACKINGRESOURCE(
    HANDLE Context,
    DXGKARG_GETBACKINGRESOURCE *pArgs);
typedef DXGKDDI_GETBACKINGRESOURCE *PDXGKDDI_GETBACKINGRESOURCE;

typedef struct _DXGKARG_GETMMIORANGECOUNT
{
    ULONG VirtualFunctionIndex;
    ULONG RangeCount[PCI_TYPE0_ADDRESSES];
} DXGKARG_GETMMIORANGECOUNT, *PDXGKARG_GETMMIORANGECOUNT;

typedef NTSTATUS DXGKDDI_GETMMIORANGECOUNT(
    HANDLE Context,
    DXGKARG_GETMMIORANGECOUNT *pArgs);
typedef DXGKDDI_GETMMIORANGECOUNT *PDXGKDDI_GETMMIORANGECOUNT;

#define DXGK_MMIO_RANGES_EMULATED_PAGE 0xFFFFFFFFFFFFFFFFULL

typedef struct _DXGK_MMIORANGEINFO
{
    ULONG64 BasePageNumber;
    ULONG64 BasePhysicalPageNumber;
    UCHAR BasePhysicalResourceNumber;
    BOOLEAN InterceptReads;
    BOOLEAN InterceptWrites;
    ULONG PageCount;
} DXGK_MMIORANGEINFO, *PDXGK_MMIORANGEINFO;

typedef struct _DXGKARG_GETMMIORANGES
{
    ULONG VirtualFunctionIndex;
    ULONG BarIndex;
    ULONG NumRanges;
    DXGK_MMIORANGEINFO *pMmioRanges;
} DXGKARG_GETMMIORANGES, *PDXGKARG_GETMMIORANGES;

typedef NTSTATUS DXGKDDI_GETMMIORANGES(
    HANDLE Context,
    PDXGKARG_GETMMIORANGES pArgs);
typedef DXGKDDI_GETMMIORANGES *PDXGKDDI_GETMMIORANGES;

typedef struct _DXGKDDI_FLEXIOV_DEVICE_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDXGKDDI_GETBACKINGRESOURCE DxgkDdiGetBackingResource;
    PDXGKDDI_GETMMIORANGECOUNT DxgkDdiGetMmioRangeCount;
    PDXGKDDI_GETMMIORANGES DxgkDdiGetMmioRanges;
} DXGKDDI_FLEXIOV_DEVICE_INTERFACE, *PDXGKDDI_FLEXIOV_DEVICE_INTERFACE;

#define DXGKDDI_FLEXIOV_DEVICE_INTERFACE_VERSION 1

#endif

DEFINE_GUID(GUID_DXGK_MIPI_DSI_INTERFACE,
            0x14f9db8b, 0x85e1, 0x4aa5, 0x8d, 0xaf, 0xff, 0x4a, 0x78, 0x06, 0xd5, 0xe9);

#define DXGK_MIPI_DSI_INTERFACE_VERSION_1 0x1

typedef struct _DXGK_DSI_CAPS
{
    BYTE DSITypeMajor;
    BYTE DSITypeMinor;
    BYTE SpecVersionMajor;
    BYTE SpecVersionMinor;
    BYTE SpecVersionPatch;
    WORD TargetMaximumReturnPacketSize;
    BYTE ResultCodeFlags;
    BYTE ResultCodeStatus;
    BYTE Revision;
    BYTE Level;
    BYTE DeviceClassHi;
    BYTE DeviceClassLo;
    BYTE ManufacturerHi;
    BYTE ManufacturerLo;
    BYTE ProductHi;
    BYTE ProductLo;
    BYTE LengthHi;
    BYTE LengthLo;
} DXGK_DSI_CAPS, *PDXGK_DSI_CAPS;

typedef NTSTATUS DXGKDDI_DSICAPS(
    HANDLE Context,
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGK_DSI_CAPS pArgs);
typedef DXGKDDI_DSICAPS *PDXGKDDI_DSICAPS;

typedef enum _DXGK_DSI_CONTROL_TRANSMISSION_MODE
{
    DXGK_DCT_DEFAULT = 0,
    DXGK_DCT_FORCE_LOW_POWER,
    DXGK_DCT_FORCE_HIGH_SPEED,
} DXGK_DSI_CONTROL_TRANSMISSION_MODE;

#define DXGK_DSI_PACKET_EMBEDDED_PAYLOAD_SIZE 8

typedef struct _DXGK_DSI_PACKET
{
    union
    {
        BYTE DataId;
        struct
        {
            BYTE DataType : 6;
            BYTE VirtualChannel : 2;
        };
    };
    union
    {
        struct
        {
            BYTE Data0;
            BYTE Data1;
        };
        WORD LongWriteWordCount;
    };
    BYTE EccFiller;
    BYTE Payload[DXGK_DSI_PACKET_EMBEDDED_PAYLOAD_SIZE];
} DXGK_DSI_PACKET, *PDXGK_DSI_PACKET;

typedef struct _DXGK_DSI_TRANSMISSION
{
    UINT TotalBufferSize;
    BYTE PacketCount;
    BYTE FailedPacket;
    struct
    {
        WORD TransmissionMode : 2;
        WORD ReportMipiErrors : 1;
        WORD ClearMipiErrors : 1;
        WORD SecondaryPort : 1;
        WORD ManufacturingMode : 1;
        WORD Reserved : 10;
    };
    WORD ReadWordCount;
    WORD FinalCommandExtraPayload;
    WORD MipiErrors;
    WORD HostErrors;
    DXGK_DSI_PACKET Packets[1];
} DXGK_DSI_TRANSMISSION, *PDXGK_DSI_TRANSMISSION;

#define DXGK_MAX_PACKET_COUNT 0x80
#define DXGK_DSI_INVALID_PACKET_INDEX 0xFF
#define DXGK_DSI_SOT_ERROR 0x0001
#define DXGK_DSI_SOT_SYNC_ERROR 0x0002
#define DXGK_DSI_EOT_SYNC_ERROR 0x0004
#define DXGK_DSI_ESCAPE_MODE_ENTRY_COMMAND_ERROR 0x0008
#define DXGK_DSI_LOW_POWER_TRANSMIT_SYNC_ERROR 0x0010
#define DXGK_DSI_PERIPHERAL_TIMEOUT_ERROR 0x0020
#define DXGK_DSI_FALSE_CONTROL_ERROR 0x0040
#define DXGK_DSI_CONTENTION_DETECTED 0x0080
#define DXGK_DSI_CHECKSUM_ERROR_CORRECTED 0x0100
#define DXGK_DSI_CHECKSUM_ERROR_NOT_CORRECTED 0x0200
#define DXGK_DSI_LONG_PACKET_PAYLOAD_CHECKSUM_ERROR 0x0400
#define DXGK_DSI_DSI_DATA_TYPE_NOT_RECOGNIZED 0x0800
#define DXGK_DSI_DSI_VC_ID_INVALID 0x1000
#define DXGK_DSI_INVALID_TRANSMISSION_LENGTH 0x2000
#define DXGK_DSI_DSI_PROTOCOL_VIOLATION 0x8000
#define DXGK_HOST_DSI_DEVICE_NOT_READY 0x0001
#define DXGK_HOST_DSI_INTERFACE_RESET 0x0002
#define DXGK_HOST_DSI_DEVICE_RESET 0x0004
#define DXGK_HOST_DSI_TRANSMISSION_CANCELLED 0x0010
#define DXGK_HOST_DSI_TRANSMISSION_DROPPED 0x0020
#define DXGK_HOST_DSI_TRANSMISSION_TIMEOUT 0x0040
#define DXGK_HOST_DSI_INVALID_TRANSMISSION 0x0100
#define DXGK_HOST_DSI_OS_REJECTED_PACKET 0x0200
#define DXGK_HOST_DSI_DRIVER_REJECTED_PACKET 0x0400
#define DXGK_HOST_DSI_BAD_TRANSMISSION_MODE 0x1000

typedef NTSTATUS DXGKDDI_DSITRANSMISSION(
    HANDLE Context,
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGK_DSI_TRANSMISSION pArgs);
typedef DXGKDDI_DSITRANSMISSION *PDXGKDDI_DSITRANSMISSION;

typedef struct _DXGK_DSI_RESET
{
    UINT Flags;
    union
    {
        struct
        {
            UINT MipiErrors : 16;
            UINT ResetFailed : 1;
            UINT NeedModeSet : 1;
        };
        UINT Results;
    };
} DXGK_DSI_RESET, *PDXGK_DSI_RESET;

typedef NTSTATUS DXGKDDI_DSIRESET(
    HANDLE Context,
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGK_DSI_RESET pArgs);
typedef DXGKDDI_DSIRESET *PDXGKDDI_DSIRESET;

typedef struct _DXGK_MIPI_DSI_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDXGKDDI_DSICAPS DxgkDdiDsiCaps;
    PDXGKDDI_DSITRANSMISSION DxgkDdiDsiTransmission;
    PDXGKDDI_DSIRESET DxgkDdiDsiReset;
} DXGK_MIPI_DSI_INTERFACE, *PDXGK_MIPI_DSI_INTERFACE;

DEFINE_GUID(GUID_DXGK_DISPLAY_DIAGNOSTICS_INTERFACE,
            0x962639f3, 0xe9dc, 0x42ab, 0x94, 0xeb, 0x06, 0x51, 0x6d, 0xec, 0xa1, 0x26);

#define DXGK_DISPLAY_DIAGNOSTICS_INTERFACE_VERSION_1 0x1

typedef enum _DXGK_DIAG_DISPLAY_CONNECTIVITY
{
    DXGK_DIAG_DISPLAY_CONNECTIVITY_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_NOT_CONNECTED = 1,
    DXGK_DIAG_DISPLAY_CONNECTED = 2,
} DXGK_DIAG_DISPLAY_CONNECTIVITY;

typedef enum _DXGK_DIAG_BASIC_DISPLAY_TOPOLOGY
{
    DXGK_DIAG_BASIC_DISPLAY_TOPOLOGY_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_CONNECTED_DIRECTLY = 1,
    DXGK_DIAG_DISPLAY_CONNECTED_INDIRECTLY_CONVERTOR = 2,
    DXGK_DIAG_DISPLAY_CONNECTED_INDIRECTLY_HUB = 3,
    DXGK_DIAG_DISPLAY_CONNECTED_INDIRECTLY = 4,
    DXGK_DIAG_DISPLAY_CONNECTED_UNKNOWN = 5,
} DXGK_DIAG_BASIC_DISPLAY_TOPOLOGY;

typedef enum _DXGK_DIAG_DISPLAY_LINK_STATE
{
    DXGK_DIAG_DISPLAY_LINK_STATE_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_LINK_STATE_NOTAPPLICABLE = 1,
    DXGK_DIAG_DISPLAY_LINK_STATE_STABLE = 2,
    DXGK_DIAG_DISPLAY_LINK_STATE_FAILED = 3,
    DXGK_DIAG_DISPLAY_LINK_STATE_CONTINUOUS_TRAINING = 4,
    DXGK_DIAG_DISPLAY_LINK_STATE_CONTINUOUS_TRAINING_STABLE = 5,
    DXGK_DIAG_DISPLAY_LINK_STATE_CONTINUOUS_TRAINING_FAILED = 6,
} DXGK_DIAG_DISPLAY_LINK_STATE;

typedef enum _DXGK_DIAG_DISPLAY_MODE_SET
{
    DXGK_DIAG_DISPLAY_MODE_SET_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_MODE_SET_NO = 1,
    DXGK_DIAG_DISPLAY_MODE_SET_YES = 2,
} DXGK_DIAG_DISPLAY_MODE_SET;

typedef enum _DXGK_DIAG_DISPLAY_LID_STATE
{
    DXGK_DIAG_DISPLAY_LID_STATE_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_LID_STATE_NOTAPPLICABLE = 1,
    DXGK_DIAG_DISPLAY_LID_STATE_OPEN = 2,
    DXGK_DIAG_DISPLAY_LID_STATE_CLOSE = 3,
    DXGK_DIAG_DISPLAY_LID_STATE_UNKNOWN = 4,
} DXGK_DIAG_DISPLAY_LID_STATE;

typedef enum _DXGK_DIAG_GETDISPLAYSTATE_SUBSTATUS_FLAGS
{
    DXGK_DIAG_GETDISPLAYSTATE_SUCCESS = 0x0,
    DXGK_DIAG_GETDISPLAYSTATE_CAUSED_GLITCH = 0x1,
    DXGK_DIAG_GETDISPLAYSTATE_CHANGED_DISPLAY_STATE = 0x2,
    DXGK_DIAG_GETDISPLAYSTATE_MONITOR_NOT_CONNECTED = 0x4,
    DXGK_DIAG_GETDISPLAYSTATE_TIMEOUT = 0x8,
    DXGK_DIAG_GETDISPLAYSTATE_ERROR_HARDWARE = 0x10,
    DXGK_DIAG_GETDISPLAYSTATE_ERROR_DRIVER = 0x20,
    DXGK_DIAG_GETDISPLAYSTATE_VIDPNTARGETID_NOT_FOUND = 0x40,
} DXGK_DIAG_GETDISPLAYSTATE_SUBSTATUS_FLAGS;

typedef struct _DXGK_DISPLAYSTATE_NONINTRUSIVE
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
    DXGK_DIAG_DISPLAY_CONNECTIVITY DisplayConnectivity;
    DXGK_DIAG_DISPLAY_LID_STATE DisplayLidState;
    DXGK_DIAG_BASIC_DISPLAY_TOPOLOGY DisplayTopology;
    DXGK_DIAG_DISPLAY_LINK_STATE DisplayLinkState;
    DXGK_DIAG_DISPLAY_MODE_SET DisplayModeSet;
    UINT ReturnSubStatus;
} DXGK_DISPLAYSTATE_NONINTRUSIVE;

typedef struct _DXGKARG_GETDISPLAYSTATE_NONINTRUSIVE
{
    UINT NumOfTargets;
    UINT SizeOfDisplayStateNonIntrusiveElement;
    DXGK_DISPLAYSTATE_NONINTRUSIVE **ppDisplayStateNonIntrusive;
} DXGKARG_GETDISPLAYSTATENONINTRUSIVE, *PDXGKARG_GETDISPLAYSTATENONINTRUSIVE;

typedef NTSTATUS (*DXGKDDI_GETDISPLAYSTATENONINTRUSIVE)(
    HANDLE Context,
    PDXGKARG_GETDISPLAYSTATENONINTRUSIVE pArgs);

typedef enum _DXGK_DIAG_MONITOR_STATE
{
    DXGK_DIAG_MONITOR_STATE_UNINITIALIZED = 0,
    DXGK_DIAG_MONITOR_READY = 1,
    DXGK_DIAG_MONITOR_NOT_READY = 2,
    DXGK_DIAG_MONITOR_READY_NOTAPPLICABLE = 3,
} DXGK_DIAG_MONITOR_STATE;

typedef enum _DXGK_DIAG_DISPLAY_SCANOUT_STATE
{
    DXGK_DIAG_DISPLAY_SCANOUT_STATE_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_SCANOUT_DISABLED = 1,
    DXGK_DIAG_DISPLAY_SCANOUT_ACTIVE = 2,
    DXGK_DIAG_DISPLAY_SCANOUT_ACTIVE_BLACK = 3,
} DXGK_DIAG_DISPLAY_SCANOUT_STATE;

#define MAX_NUM_OF_GAMMA_SAMPLES_FOR_DIAGNOSTICS 16

typedef struct _DXGK_DIAG_DISPLAY_SAMPLED_GAMMA
{
    float Red[MAX_NUM_OF_GAMMA_SAMPLES_FOR_DIAGNOSTICS];
    float Green[MAX_NUM_OF_GAMMA_SAMPLES_FOR_DIAGNOSTICS];
    float Blue[MAX_NUM_OF_GAMMA_SAMPLES_FOR_DIAGNOSTICS];
    float ColorMatrix[3][3];
} DXGK_DIAG_DISPLAY_SAMPLED_GAMMA;

typedef enum _DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_CRC
{
    DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_CRC_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_CRC_BLACK = 1,
    DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_CRC_NON_BLACK = 2,
    DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_CRC_ERROR = 3,
    DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_CRC_UNKNOWN = 4,
} DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_CRC;

typedef struct _DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_HISTOGRAM
{
    INT MinPixelValue;
    INT MaxPixelValue;
} DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_HISTOGRAM;

typedef struct _DXGK_DIAG_SCANOUT_BUFFER_CONTENT
{
    DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_CRC ScanoutBufferCrc;
    DXGK_DIAG_DISPLAY_SCANOUT_BUFFER_HISTOGRAM ScanoutBufferHistogram;
} DXGK_DIAG_SCANOUT_BUFFER_CONTENT;

typedef enum _DXGK_DIAG_DISPLAY_HARDWARE_ERROR_STATE
{
    DXGK_DIAG_DISPLAY_HARDWARE_ERROR_STATE_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_HARDWARE_ERROR_NONE = 1,
    DXGK_DIAG_DISPLAY_HARDWARE_ERROR_SCANOUT_UNDERFLOW = 2,
    DXGK_DIAG_DISPLAY_HARDWARE_ERROR_TDRNORECOVERY = 3,
    DXGK_DIAG_DISPLAY_HARDWARE_ERROR_UNSPECIFIED = 4,
} DXGK_DIAG_DISPLAY_HARDWARE_ERROR_STATE;

typedef enum _DXGK_DIAG_DISPLAY_HARDWARE_BANDWIDTH
{
    DXGK_DIAG_DISPLAY_HARDWARE_BANDWIDTH_UNINITIALIZED = 0,
    DXGK_DIAG_DISPLAY_HARDWARE_BANDWIDTH_SUFFICIENT = 1,
    DXGK_DIAG_DISPLAY_HARDWARE_LINK_BANDWIDTH_LIMITED = 2,
    DXGK_DIAG_DISPLAY_HARDWARE_SOC_BANDWIDTH_LIMITED = 3,
    DXGK_DIAG_DISPLAY_HARDWARE_BANDWIDTH_ERROR = 4,
    DXGK_DIAG_DISPLAY_HARDWARE_BANDWIDTH_UNKNOWN = 5,
} DXGK_DIAG_DISPLAY_HARDWARE_BANDWIDTH;

typedef struct _DXGKARG_DISPLAYSTATE_INTRUSIVE
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
    DXGK_DIAG_MONITOR_STATE MonitorState;
    DXGK_DIAG_DISPLAY_SCANOUT_STATE DisplayScanoutState;
    DXGK_DIAG_DISPLAY_SAMPLED_GAMMA DisplaySampledGamma;
    DXGK_DIAG_SCANOUT_BUFFER_CONTENT DisplayBufferContent;
    DXGK_DIAG_DISPLAY_HARDWARE_ERROR_STATE DisplayErrorState;
    DXGK_DIAG_DISPLAY_HARDWARE_BANDWIDTH DisplayBandwidth;
    UINT ReturnSubStatus;
} DXGK_DISPLAYSTATE_INTRUSIVE;

typedef struct _DXGKARG_GETDISPLAYSTATE_INTRUSIVE
{
    UINT NumOfTargets;
    UINT SizeOfDisplayStateIntrusiveElement;
    DXGK_DISPLAYSTATE_INTRUSIVE **ppDisplayStateIntrusive;
} DXGKARG_GETDISPLAYSTATEINTRUSIVE, *PDXGKARG_GETDISPLAYSTATEINTRUSIVE;

typedef NTSTATUS (*DXGKDDI_GETDISPLAYSTATEINTRUSIVE)(
    HANDLE Context,
    PDXGKARG_GETDISPLAYSTATEINTRUSIVE pArgs);

typedef struct _DXGK_DISPLAY_DIAGNOSTICS_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    DXGKDDI_GETDISPLAYSTATENONINTRUSIVE DxgkDdiGetDisplayStateNonIntrusive;
    DXGKDDI_GETDISPLAYSTATEINTRUSIVE DxgkDdiGetDisplayStateIntrusive;
} DXGK_DISPLAY_DIAGNOSTICS_INTERFACE, *PDXGK_DISPLAY_DIAGNOSTICS_INTERFACE;

DEFINE_GUID(GUID_DXGK_DP_INTERFACE,
            0x2d09818e, 0xdfeb, 0x4173, 0xb5, 0xe9, 0xae, 0xfd, 0x66, 0xb2, 0x02, 0xf3);

#define DXGK_DP_INTERFACE_VERSION_1 0x1

typedef struct _DXGKARG_QUERYDPCAPS
{
    UINT NumRootPorts;
    BYTE DPVersionMajor;
    BYTE DPVersionMinor;
} DXGKARG_QUERYDPCAPS, *PDXGKARG_QUERYDPCAPS;

typedef NTSTATUS DXGKDDI_QUERYDPCAPS(
    HANDLE Context,
    PDXGKARG_QUERYDPCAPS pArgs);
typedef DXGKDDI_QUERYDPCAPS *PDXGKDDI_QUERYDPCAPS;

#define MAX_DP_ADDRESS_SIZE 15

typedef struct _DXGKARG_GETDPADDRESS
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    UINT DPNativeError;
    UINT RootPortIndex;
    BYTE NumLinks;
    BYTE RelAddress[MAX_DP_ADDRESS_SIZE];
} DXGKARG_GETDPADDRESS, *PDXGKARG_GETDPADDRESS;

typedef NTSTATUS DXGKDDI_GETDPADDRESS(
    HANDLE Context,
    PDXGKARG_GETDPADDRESS pArgs);
typedef DXGKDDI_GETDPADDRESS *PDXGKDDI_GETDPADDRESS;

#define MAX_DP_NATIVE_AUX_IO_SIZE 16

typedef struct _DXGKARG_DPAUXIOTRANSMISSION
{
    struct
    {
        UINT Write : 1;
        UINT CanUseCachedData : 1;
        UINT Reserved : 30;
    };
    UINT RootPortIndex;
    UINT DPCDAddress;
    BYTE NumBytesRequested;
    UINT DPNativeError;
    BYTE NumBytesDone;
    BYTE Data[MAX_DP_NATIVE_AUX_IO_SIZE];
} DXGKARG_DPAUXIOTRANSMISSION, *PDXGKARG_DPAUXIOTRANSMISSION;

typedef NTSTATUS DXGKDDI_DPAUXIOTRANSMISSION(
    HANDLE Context,
    PDXGKARG_DPAUXIOTRANSMISSION pArgs);
typedef DXGKDDI_DPAUXIOTRANSMISSION *PDXGKDDI_DPAUXIOTRANSMISSION;

typedef enum _DXGK_I2C_ADDRESS_TYPE
{
    DXGK_I2C_ADDRESS_EDDC_SEGMENT_POINT = 0x60,
    DXGK_I2C_ADDRESS_MCCS = 0x6E,
    DXGK_I2C_ADDRESS_DDC = 0xA0,
    DXGK_I2C_ADDRESS_MAX = 0x7F,
} DXGK_I2C_ADDRESS_TYPE;

typedef struct _DXGKARG_DPI2CIOTRANSMISSION
{
    struct
    {
        UINT Read : 1;
        UINT Write : 1;
        UINT EDDCMode : 1;
        UINT OffsetSizeInBytes : 3;
        UINT CanUseCachedData : 1;
        UINT Reserved : 25;
    };
    UINT RootPortIndex;
    UINT I2CAddress;
    union
    {
        struct
        {
            UINT WordOffset : 8;
            UINT SegmentPointer : 7;
            UINT Reserved1 : 17;
        };
        UINT Offset;
    };
    UINT BufferSizeSupplied;
    UINT BytesToWrite;
    UINT BytesToRead;
    UINT DPNativeError;
    UINT BytesWritten;
    UINT BytesRead;
    BYTE Data[1];
} DXGKARG_DPI2CIOTRANSMISSION, *PDXGKARG_DPI2CIOTRANSMISSION;

typedef NTSTATUS DXGKDDI_DPI2CIOTRANSMISSION(
    HANDLE Context,
    PDXGKARG_DPI2CIOTRANSMISSION pArgs);
typedef DXGKDDI_DPI2CIOTRANSMISSION *PDXGKDDI_DPI2CIOTRANSMISSION;

typedef struct _DXGKARG_DPSBMTRANSMISSION
{
    struct
    {
        UINT CanUseCachedData : 1;
        UINT Reserved : 31;
    };
    UINT RootPortIndex;
    UINT BufferSizeSupplied;
    UINT RequestLength;
    UINT MaxReplyLength;
    UINT DPNativeError;
    UINT ActualReplyLength;
    BYTE Data[1];
} DXGKARG_DPSBMTRANSMISSION, *PDXGKARG_DPSBMTRANSMISSION;

typedef NTSTATUS DXGKDDI_DPSBMTRANSMISSION(
    HANDLE Context,
    PDXGKARG_DPSBMTRANSMISSION pArgs);
typedef DXGKDDI_DPSBMTRANSMISSION *PDXGKDDI_DPSBMTRANSMISSION;

typedef struct _DXGK_DP_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDXGKDDI_QUERYDPCAPS DxgkDdiQueryDPCaps;
    PDXGKDDI_GETDPADDRESS DxgkDdiGetDPAddress;
    PDXGKDDI_DPAUXIOTRANSMISSION DxgkDdiDPAuxIoTransmission;
    PDXGKDDI_DPI2CIOTRANSMISSION DxgkDdiDPI2CIoTransmission;
    PDXGKDDI_DPSBMTRANSMISSION DxgkDdiDPSBMTransmission;
} DXGK_DP_INTERFACE, *PDXGK_DP_INTERFACE;


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

typedef enum _DEBUG_LEVEL
{
    DlDebugError,
    DlDebugWarning,
    DlDebugTrace,
    DlDebugInfo,
} DEBUG_LEVEL;


#pragma warning(pop)

#endif /* _DISPMPRT_H_ */
