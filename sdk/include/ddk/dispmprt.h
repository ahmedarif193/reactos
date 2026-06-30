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
} QUERY_INTERFACE, *PQUERY_INTERFACE;

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
 * These are defined in d3dukmdt.h; replicated here for convenience so that
 * drivers need not include d3dukmdt.h directly.
 */
#ifndef DXGKDDI_INTERFACE_VERSION_VISTA
/* Windows Vista (WDDM 1.0) */
#define DXGKDDI_INTERFACE_VERSION_VISTA         0x1052
/* Windows Vista SP1 / Windows Server 2008 (WDDM 1.1) */
#define DXGKDDI_INTERFACE_VERSION_VISTA_SP1     0x1053
/* Windows 7 (WDDM 1.1) */
#define DXGKDDI_INTERFACE_VERSION_WIN7          0x2005
/* Windows 8 (WDDM 1.2) */
#define DXGKDDI_INTERFACE_VERSION_WIN8          0x300E
/* Windows 8.1 (WDDM 1.3) */
#define DXGKDDI_INTERFACE_VERSION_WDDM1_3      0x4002
/* Windows 10 (WDDM 2.0) */
#define DXGKDDI_INTERFACE_VERSION_WDDM2_0      0x5023
#endif /* !DXGKDDI_INTERFACE_VERSION_VISTA */

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
#define DXGK_INTERRUPT_DISPLAYONLY_VSYNC                    5
#define DXGK_INTERRUPT_DISPLAYONLY_PRESENT_PROGRESS         6
#define DXGK_INTERRUPT_PERIODICED_MONITORED_FENCE_SIGNALED  7


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

typedef struct _DXGK_CHILD_CAPABILITIES
{
    DXGK_CHILD_DEVICE_HPD_AWARENESS    HpdAwareness;
    union
    {
        DXGK_VIDEO_OUTPUT_CAPABILITIES VideoOutput;
        struct
        {
            UINT    Reserved;
        } Other;
    } Type;
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
} DXGK_CHILD_STATUS_TYPE;

typedef struct _DXGK_CHILD_STATUS
{
    DXGK_CHILD_STATUS_TYPE  Type;
    ULONG                   ChildUid;
    union
    {
        struct { BOOLEAN Connected; } HotPlug;
        struct { UCHAR   Angle;     } Rotation;
    };
} DXGK_CHILD_STATUS, *PDXGK_CHILD_STATUS;


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
typedef struct _DXGK_DEVICE_INFO
{
    PDEVICE_OBJECT      PhysicalDeviceObject;
    PUNICODE_STRING     MiniportRegistryPath;
    PHYSICAL_ADDRESS    SegmentAperture;
    ULONGLONG           SegmentFrameBufferLength;
    PCM_RESOURCE_LIST   TranslatedResourceList;
    ULONG               SystemIoBusNumber;
    INTERFACE_TYPE      AdapterInterfaceType;
    ULONG               BusInterruptLevel;
    ULONG               BusInterruptVector;
    KINTERRUPT_MODE     InterruptMode;
    ULONG               DmaAddressWidth;
    ULONG               AgpApertureBase;
    ULONG               AgpApertureSize;
    ULONG               SystemMemorySize;
} DXGK_DEVICE_INFO, *PDXGK_DEVICE_INFO;


/* =========================================================================
 * DXGK_START_FLAGS / DXGK_START_INFO
 *
 * Passed as the first argument to DxgkDdiStartDevice.
 * =========================================================================
 */
typedef union _DXGK_START_FLAGS
{
    struct
    {
        UINT    Reserved    : 32;
    };
    UINT    Value;
} DXGK_START_FLAGS;

typedef struct _DXGK_START_INFO
{
    ULONG               RequiredDxgkVtableVersion;
    ULONG               RequiredDmaQueueEntry;
    LUID                AdapterLuid;
    DXGK_START_FLAGS    StartFlags;
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


/* =========================================================================
 * DISPLAY_ADAPTER_HW_ID
 *
 * Special UID passed to DxgkDdiSetPowerState to target the adapter itself
 * rather than a child display output.
 * =========================================================================
 */
#ifndef DISPLAY_ADAPTER_HW_ID
#define DISPLAY_ADAPTER_HW_ID   0xFFFEFFFFUL
#endif


/* =========================================================================
 * DxgkCb* service callback typedefs
 *
 * These are function pointers filled in by dxgkrnl in the DXGK_INTERFACE
 * structure that is handed to the miniport's DxgkDdiStartDevice callback.
 * =========================================================================
 */

/* Notify dxgkrnl of a GPU interrupt event (called from ISR at DIRQL). */
typedef NTSTATUS
(APIENTRY *PDXGKCB_NOTIFY_INTERRUPT)(
    _In_ HANDLE                             hAdapter,
    _In_ CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA *NotifyInterruptData);

/*
 * Notify dxgkrnl that the DPC triggered by NotifyInterrupt has run
 * (called from DPC at DISPATCH_LEVEL).
 */
typedef VOID
(APIENTRY *PDXGKCB_NOTIFY_DPC)(
    _In_ HANDLE hAdapter);

/* Retrieve hardware resource information populated during StartDevice. */
typedef NTSTATUS
(APIENTRY *PDXGKCB_GET_DEVICE_INFORMATION)(
    _In_  HANDLE                hAdapter,
    _Out_ PDXGK_DEVICE_INFO     DeviceInformation);

/* Report a child device connection/rotation status change. */
typedef NTSTATUS
(APIENTRY *PDXGKCB_INDICATE_CHILD_STATUS)(
    _In_ HANDLE             hAdapter,
    _In_ PDXGK_CHILD_STATUS ChildStatus);

/*
 * Synchronize a routine with the GPU interrupt service routine.
 * Equivalent to KeSynchronizeExecution for the adapter's interrupt.
 */
typedef NTSTATUS
(APIENTRY *PDXGKCB_SYNCHRONIZE_EXECUTION)(
    _In_  HANDLE                    hAdapter,
    _In_  PKSYNCHRONIZE_ROUTINE     SynchronizeRoutine,
    _In_  PVOID                     Context,
    _In_  ULONG                     MessageNumber,
    _Out_ PBOOLEAN                  ReturnValue);

/* Allocate physically contiguous GPU-accessible memory. */
typedef NTSTATUS
(APIENTRY *PDXGKCB_ALLOCATE_CONTIGUOUS_MEMORY)(
    _In_    HANDLE  hAdapter,
    _Inout_ PVOID   AllocContiguousMemory);

/* Free memory allocated by AllocateContiguousMemory. */
typedef NTSTATUS
(APIENTRY *PDXGKCB_FREE_CONTIGUOUS_MEMORY)(
    _In_ HANDLE hAdapter,
    _In_ PVOID  FreeContiguousMemory);

/* Map a physical address range into kernel virtual address space. */
typedef NTSTATUS
(APIENTRY *PDXGKCB_MAP_PHYSICAL_MEMORY)(
    _In_    HANDLE  hAdapter,
    _Inout_ PVOID   MapPhysicalMemory);

/* Unmap a range previously mapped by MapPhysicalMemory. */
typedef NTSTATUS
(APIENTRY *PDXGKCB_UNMAP_PHYSICAL_MEMORY)(
    _In_ HANDLE hAdapter,
    _In_ PVOID  UnmapPhysicalMemory);

/*
 * Acquire ownership of the post-display information (DXGK_DISPLAY_INFORMATION)
 * from the system firmware / boot graphics driver.
 * Available on Win8+ (WDDM 1.2); may be NULL on Vista/Win7.
 */
typedef NTSTATUS
(APIENTRY *PDXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP)(
    _In_  HANDLE   hAdapter,
    _Out_ PVOID    DisplayInformation);

/*
 * Map a range of translated physical addresses (BAR regions) into
 * kernel virtual address space or user-mode process address space.
 * This is the WDDM 1.0 memory-mapping callback (distinct from the
 * WDDM 2.9 DxgkCbMapPhysicalMemory).
 */
typedef NTSTATUS
(APIENTRY *PDXGKCB_MAP_MEMORY)(
    _In_  HANDLE              hAdapter,
    _In_  PHYSICAL_ADDRESS    TranslatedAddress,
    _In_  ULONG               Length,
    _In_  BOOLEAN             InIoSpace,
    _In_  BOOLEAN             MapToUserMode,
    _In_  MEMORY_CACHING_TYPE CacheType,
    _Out_ PVOID              *VirtualAddress);

/* Unmap a range previously mapped by DxgkCbMapMemory. */
typedef NTSTATUS
(APIENTRY *PDXGKCB_UNMAP_MEMORY)(
    _In_ HANDLE hAdapter,
    _In_ PVOID  VirtualAddress);

/* Queue a DPC for the display adapter (called from ISR at any IRQL). */
typedef BOOLEAN
(APIENTRY *PDXGKCB_QUEUE_DPC)(
    _In_ HANDLE hAdapter);

/*
 * Read from a device configuration space or expansion ROM.
 * DataType: DXGK_WHICHSPACE_CONFIG, _BRIDGE, _MCH, or _ROM.
 */
typedef NTSTATUS
(APIENTRY *PDXGKCB_READ_DEVICE_SPACE)(
    _In_  HANDLE  hAdapter,
    _In_  ULONG   DataType,
    _In_  PVOID   Buffer,
    _In_  ULONG   Offset,
    _In_  ULONG   Length,
    _Out_ PULONG  BytesRead);

/*
 * Write to a device configuration space.
 * DataType: DXGK_WHICHSPACE_CONFIG, _BRIDGE, _MCH, or _ROM.
 */
typedef NTSTATUS
(APIENTRY *PDXGKCB_WRITE_DEVICE_SPACE)(
    _In_  HANDLE  hAdapter,
    _In_  ULONG   DataType,
    _In_  PVOID   Buffer,
    _In_  ULONG   Offset,
    _In_  ULONG   Length,
    _Out_ PULONG  BytesWritten);

typedef enum _DXGK_HANDLE_TYPE
{
    DXGK_HANDLE_ALLOCATION = 1,
    DXGK_HANDLE_RESOURCE   = 2,
    DXGK_HANDLE_DEVICE     = 3,
    DXGK_HANDLE_CONTEXT    = 4
} DXGK_HANDLE_TYPE, *PDXGK_HANDLE_TYPE;

typedef union _DXGK_GETHANDLEDATAFLAGS
{
    struct
    {
        UINT DeviceSpecific : 1;
        UINT Reserved       : 31;
    };
    UINT Value;
} DXGK_GETHANDLEDATAFLAGS, *PDXGK_GETHANDLEDATAFLAGS;

typedef struct _DXGKARGCB_GETHANDLEDATA
{
    D3DKMT_HANDLE           hObject;
    DXGK_HANDLE_TYPE        Type;
    DXGK_GETHANDLEDATAFLAGS Flags;
} DXGKARGCB_GETHANDLEDATA, *PDXGKARGCB_GETHANDLEDATA;

typedef PVOID
(APIENTRY *PDXGKCB_GETHANDLEDATA)(
    _In_ PDXGKARGCB_GETHANDLEDATA HandleData);

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

/* =========================================================================
 * DXGK_MONITORSOURCEMODESET_INTERFACE
 *
 * Function table for manipulating a monitor source mode set.
 * =========================================================================
 */
typedef struct _DXGK_MONITORSOURCEMODESET_INTERFACE
{
    NTSTATUS (APIENTRY *pfnGetNumModes)(
        _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
        _Out_ CONST SIZE_T*                              pNumModes);

    NTSTATUS (APIENTRY *pfnAcquirePreferredModeInfo)(
        _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
        _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppFirstMonitorSourceModeInfo);

    NTSTATUS (APIENTRY *pfnAcquireFirstModeInfo)(
        _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
        _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppFirstMonitorSourceModeInfo);

    NTSTATUS (APIENTRY *pfnAcquireNextModeInfo)(
        _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
        _In_  CONST D3DKMDT_MONITOR_SOURCE_MODE*         pMonitorSourceModeInfo,
        _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppNextMonitorSourceModeInfo);

    NTSTATUS (APIENTRY *pfnCreateNewModeInfo)(
        _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
        _Out_ D3DKMDT_MONITOR_SOURCE_MODE**              ppNewMonitorSourceModeInfo);

    NTSTATUS (APIENTRY *pfnAddMode)(
        _In_ D3DKMDT_HMONITORSOURCEMODESET               hMonitorSourceModeSet,
        _In_ D3DKMDT_MONITOR_SOURCE_MODE*                pMonitorSourceModeInfo);

    NTSTATUS (APIENTRY *pfnReleaseModeInfo)(
        _In_ D3DKMDT_HMONITORSOURCEMODESET               hMonitorSourceModeSet,
        _In_ CONST D3DKMDT_MONITOR_SOURCE_MODE*          pMonitorSourceModeInfo);
} DXGK_MONITORSOURCEMODESET_INTERFACE;

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
typedef NTSTATUS
(APIENTRY *PDXGKCB_QUERYVIDPNINTERFACE)(
    _In_  D3DKMDT_HVIDPN                       hVidPn,
    _In_  DXGK_VIDPN_INTERFACE_VERSION         VidPnInterfaceVersion,
    _Out_ CONST DXGK_VIDPN_INTERFACE**         ppVidPnInterface);

typedef NTSTATUS
(APIENTRY *PDXGKCB_QUERYMONITORINTERFACE)(
    _In_  HANDLE                                   hAdapter,
    _In_  UINT                                     MonitorInterfaceVersion,
    _Out_ PVOID*                                   ppMonitorInterface);

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

/*
 * Reserved for OS use by asynchronous KMDOD presents.
 * ReactOS currently drives display-only presents synchronously and
 * always sets the callback pointer to NULL.
 */
typedef struct _DXGKARGCB_PRESENT_DISPLAYONLY_PROGRESS
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID               VidPnSourceId;
    ULONG                                        ProgressId;
} DXGKARGCB_PRESENT_DISPLAYONLY_PROGRESS,
 *PDXGKARGCB_PRESENT_DISPLAYONLY_PROGRESS;

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
    PVOID   DxgkCbEvalAcpiMethod;                       /* 0x10 */
    PDXGKCB_GET_DEVICE_INFORMATION DxgkCbGetDeviceInformation; /* 0x18 */
    PDXGKCB_INDICATE_CHILD_STATUS DxgkCbIndicateChildStatus;   /* 0x20 */
    PDXGKCB_MAP_MEMORY        DxgkCbMapMemory;          /* 0x28 */
    PDXGKCB_QUEUE_DPC         DxgkCbQueueDpc;           /* 0x30 */
    PVOID   DxgkCbQueryServices;                        /* 0x38 */
    PDXGKCB_READ_DEVICE_SPACE DxgkCbReadDeviceSpace;    /* 0x40 */
    PDXGKCB_SYNCHRONIZE_EXECUTION DxgkCbSynchronizeExecution; /* 0x48 */
    PDXGKCB_UNMAP_MEMORY      DxgkCbUnmapMemory;       /* 0x50 */
    PDXGKCB_WRITE_DEVICE_SPACE DxgkCbWriteDeviceSpace;  /* 0x58 */
    PVOID   DxgkCbIsDevicePresent;                      /* 0x60 */
    PDXGKCB_GETHANDLEDATA DxgkCbGetHandleData;          /* 0x68 */
    PVOID   DxgkCbGetHandleParent;                      /* 0x70 */
    PVOID   DxgkCbEnumHandleChildren;                   /* 0x78 */
    PDXGKCB_NOTIFY_INTERRUPT  DxgkCbNotifyInterrupt;    /* 0x80 */
    PDXGKCB_NOTIFY_DPC        DxgkCbNotifyDpc;          /* 0x88 */
    PDXGKCB_QUERYVIDPNINTERFACE    DxgkCbQueryVidPnInterface;    /* 0x90 */
    PDXGKCB_QUERYMONITORINTERFACE  DxgkCbQueryMonitorInterface;  /* 0x98 */
    PVOID   DxgkCbGetCaptureAddress;                    /* 0xa0 */

    /* --- Vista SP1 / Win7 additions --- */
    PVOID   DxgkCbLogEtwEvent;                          /* 0xa8 */
    PVOID   DxgkCbExcludeAdapterAccess;                 /* 0xb0 */

    /* --- Win8 (WDDM 1.2) additions --- */
    PVOID   DxgkCbCreateContextAllocation;              /* 0xb8 */
    PVOID   DxgkCbDestroyContextAllocation;             /* 0xc0 */
    PVOID   DxgkCbSetPowerComponentActive;              /* 0xc8 */
    PVOID   DxgkCbSetPowerComponentIdle;                /* 0xd0 */
    PDXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP DxgkCbAcquirePostDisplayOwnership; /* 0xd8 */
    PVOID   DxgkCbPowerRuntimeControlRequest;           /* 0xe0 */
    PVOID   DxgkCbSetPowerComponentLatency;             /* 0xe8 */
    PVOID   DxgkCbSetPowerComponentResidency;           /* 0xf0 */
    PVOID   DxgkCbCompleteFStateTransition;             /* 0xf8 */

    /* --- Win8.1 (WDDM 1.3) --- */
    PVOID   DxgkCbCompletePStateTransition;             /* 0x100 */

    /* --- WDDM 2.0 additions --- */
    PVOID   DxgkCbMapContextAllocation;                 /* 0x108 */
    PVOID   DxgkCbUpdateContextAllocation;              /* 0x110 */
    PVOID   DxgkCbReserveGpuVirtualAddressRange;        /* 0x118 */
    PVOID   DxgkCbAcquireHandleData;                    /* 0x120 */
    PVOID   DxgkCbReleaseHandleData;                    /* 0x128 */
    PVOID   DxgkCbHardwareContentProtectionTeardown;    /* 0x130 */

    /* --- WDDM 2.1 additions --- */
    PVOID   DxgkCbMultiPlaneOverlayDisabled;            /* 0x138 */
    PVOID   DxgkCbMitigatedRangeUpdate;                 /* 0x140 */

    /* --- WDDM 2.2 additions --- */
    PVOID   DxgkCbInvalidateHwContext;                  /* 0x148 */
    PVOID   DxgkCbIndicateConnectorChange;              /* 0x150 */
    PVOID   DxgkCbUnblockUEFIFrameBufferRanges;         /* 0x158 */
    PVOID   DxgkCbAcquirePostDisplayOwnership2;         /* 0x160 */

    /* --- WDDM 2.3 additions --- */
    PVOID   DxgkCbSetProtectedSessionStatus;            /* 0x168 */

    /* --- WDDM 2.4 additions --- */
    PDXGKCB_ALLOCATE_CONTIGUOUS_MEMORY DxgkCbAllocateContiguousMemory; /* 0x170 */
    PDXGKCB_FREE_CONTIGUOUS_MEMORY DxgkCbFreeContiguousMemory;       /* 0x178 */
    PVOID   DxgkCbAllocatePagesForMdl;                  /* 0x180 */
    PVOID   DxgkCbFreePagesFromMdl;                     /* 0x188 */
    PVOID   DxgkCbPinFrameBufferForSave;                /* 0x190 */
    PVOID   DxgkCbUnpinFrameBufferForSave;              /* 0x198 */
    PVOID   DxgkCbMapFrameBufferPointer;                /* 0x1a0 */
    PVOID   DxgkCbUnmapFrameBufferPointer;              /* 0x1a8 */
    PVOID   DxgkCbMapMdlToIoMmu;                        /* 0x1b0 */
    PVOID   DxgkCbUnmapMdlFromIoMmu;                    /* 0x1b8 */
    PVOID   DxgkCbReportDiagnostic;                     /* 0x1c0 */

    /* --- WDDM 2.5+ additions --- */
    PVOID   DxgkCbSignalEvent;                          /* 0x1c8 */

    /* --- WDDM 2.6 additions --- */
    PVOID   DxgkCbIsFeatureEnabled;                     /* 0x1d0 */
    PVOID   DxgkCbSaveMemoryForHotUpdate;               /* 0x1d8 */

    /* --- WDDM 2.8 --- */
    PVOID   DxgkCbNotifyCursorSupportChange;            /* 0x1e0 */

    /* --- WDDM 2.9 additions --- */
    PVOID   DxgkCbQueryFeatureSupport;                  /* 0x1e8 */
    PVOID   DxgkCbCreatePhysicalMemoryObject;           /* 0x1f0 */
    PVOID   DxgkCbDestroyPhysicalMemoryObject;          /* 0x1f8 */
    PDXGKCB_MAP_PHYSICAL_MEMORY   DxgkCbMapPhysicalMemory;  /* 0x200 */
    PDXGKCB_UNMAP_PHYSICAL_MEMORY DxgkCbUnmapPhysicalMemory; /* 0x208 */
    PVOID   DxgkCbAllocateAdl;                          /* 0x210 */
    PVOID   DxgkCbFreeAdl;                              /* 0x218 */
    PVOID   DxgkCbOpenPhysicalMemoryObject;             /* 0x220 */
    PVOID   DxgkCbClosePhysicalMemoryObject;            /* 0x228 */
    PVOID   DxgkCbPinFrameBufferForSave2;               /* 0x230 */

    /* --- WDDM 3.1 --- */
    PVOID   DxgkCbDisconnectDoorbell;                   /* 0x238 */
} DXGK_INTERFACE, *PDXGK_INTERFACE;

/* Windows WDK uses DXGKRNL_INTERFACE as the official name */
typedef DXGK_INTERFACE  DXGKRNL_INTERFACE;
typedef DXGK_INTERFACE *PDXGKRNL_INTERFACE;

/* Forward-declare KMDDOD_INITIALIZATION_DATA — defined after DRIVER_INITIALIZATION_DATA */
typedef struct _KMDDOD_INITIALIZATION_DATA  KMDDOD_INITIALIZATION_DATA;
typedef struct _KMDDOD_INITIALIZATION_DATA *PKMDDOD_INITIALIZATION_DATA;

/* DxgkInitializeDisplayOnlyDriver — exported by dxgkrnl.sys */
NTSTATUS
APIENTRY
DxgkInitializeDisplayOnlyDriver(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PKMDDOD_INITIALIZATION_DATA KmDodInitializationData);


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

typedef NTSTATUS
(APIENTRY *PDXGKDDI_GET_NODE_METADATA)(
    _In_  PVOID                      MiniportDeviceContext,
    _In_  UINT                       NodeOrdinal,
    _Out_ DXGKARG_GETNODEMETADATA   *GetNodeMetadata);


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
    union
    {
        PDXGKDDI_COLLECT_DB_ENGINE_INFO         DxgkDdiCollectDbEngineInfo;
        PDXGKDDI_COLLECT_DB_ENGINE_INFO         DxgkDdiCollectDbgInfo;
    };
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
    union
    {
        PDXGKDDI_QUERY_VIDPN_HW_CAPABILITY      DxgkDdiQueryVidPnHwCapability;
        PDXGKDDI_QUERY_VIDPN_HW_CAPABILITY      DxgkDdiQueryVidPnHWCapability;
    };
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
    PVOID                                       DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay;
    PVOID                                       DxgkDdiNotifySurpriseRemoval;
    PVOID                                       DxgkDdiPresentDisplayOnly;
#endif

    /* ---- WDDM 1.3 / Win8.1 additions ------------------------------------ */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    PDXGKDDI_GET_NODE_METADATA                  DxgkDdiGetNodeMetadata;
    PVOID                                       DxgkDdiSetPowerPState;           /* reserved, set to zero */
    PVOID                                       DxgkDdiControlInterrupt2;
    PVOID                                       DxgkDdiCheckMultiPlaneOverlaySupport;
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
    PVOID                                                           DxgkDdiPowerRuntimeSetDeviceHandle;
    PVOID                                                           DxgkDdiSetStablePowerState;
    PVOID                                                           DxgkDdiSetVideoProtectedRegion;
#endif

} DRIVER_INITIALIZATION_DATA, *PDRIVER_INITIALIZATION_DATA;


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
    union
    {
        PDXGKDDI_QUERY_VIDPN_HW_CAPABILITY      DxgkDdiQueryVidPnHwCapability;
        PDXGKDDI_QUERY_VIDPN_HW_CAPABILITY      DxgkDdiQueryVidPnHWCapability;
    };

    /* Win8+ display-only callbacks */
    PVOID                                       DxgkDdiPresentDisplayOnly;
    PDXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    PDXGKDDI_SYSTEM_DISPLAY_ENABLE              DxgkDdiSystemDisplayEnable;
    PDXGKDDI_SYSTEM_DISPLAY_WRITE               DxgkDdiSystemDisplayWrite;
    PVOID                                       DxgkDdiGetChildContainerId;
    PDXGKDDI_CONTROL_INTERRUPT                  DxgkDdiControlInterrupt;
    PVOID                                       DxgkDdiSetPowerComponentFState;
    PVOID                                       DxgkDdiPowerRuntimeControlRequest;
    PVOID                                       DxgkDdiNotifySurpriseRemoval;
    PVOID                                       DxgkDdiPowerRuntimeSetDeviceHandle;
};


/* =========================================================================
 * DxgkInitialize / DxgkInitializeEx
 *
 * Entry points exported by dxgkrnl.sys.  The miniport calls DxgkInitialize
 * from its DriverEntry to register with the display kernel subsystem.
 * =========================================================================
 */

/*
 * DxgkInitialize — Vista WDDM 1.0 entry point.
 * Exported by dxgkrnl.sys; the miniport's import library must reference it.
 */
NTSTATUS
APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT                 DriverObject,
    _In_ PUNICODE_STRING                RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA    DriverInitializationData);


#pragma warning(pop)

#endif /* _DISPMPRT_H_ */
