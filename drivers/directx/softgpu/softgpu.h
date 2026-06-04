/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Private header for softgpu.sys — WDDM 1.0 software display
 *              miniport targeting QEMU STD VGA (PCI VEN_1234&DEV_1111).
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Architecture notes (amd64/x86)
 * ================================
 * softgpu is a pure software device with no real GPU hardware.  It allocates
 * a 16 MB write-combined contiguous buffer from system RAM and presents it to
 * dxgkrnl as a single CPU-visible aperture segment.
 *
 * Fence tracking uses a KSPIN_LOCK (FenceLock) to guard CurrentFence and
 * CompletedFence.  On x86 TSO only a compiler barrier is strictly required
 * for same-direction accesses, but we use InterlockedExchange semantics via
 * the spinlock to handle the cross-IRQL path (DPC vs. PASSIVE_LEVEL).
 *
 * The DPC routine (SoftGpuDpcRoutine) is queued from SubmitCommand at
 * DISPATCH_LEVEL; it calls DxgkCbNotifyInterrupt + DxgkCbNotifyDpc through
 * the DXGK_INTERFACE vtable saved at StartDevice time.
 */

#pragma once

/* ---- Kernel / WDM headers ------------------------------------------------ */
#include <ntddk.h>
#include <wdm.h>

/* ---- WDDM interface version selection ------------------------------------ */
/*
 * Target WDDM 1.0 (Vista) for the initial implementation.
 */
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_VISTA
#endif

/* ---- Windows base type compatibility ------------------------------------- */
/*
 * d3dukmdt.h / d3dkmdt.h use Windows base types (UINT, BYTE, DWORD, BOOL,
 * POINT, RECT) that are normally defined in user-mode headers (windef.h /
 * wtypes.h) but are NOT pulled in by the kernel headers (ntddk.h / wdm.h).
 * Provide them here with guards matching the definitions in psdk/wtypes.h.
 *
 * These definitions are stable across all x86/amd64 builds:
 *   UINT  = unsigned int  (32-bit always)
 *   BYTE  = unsigned char (8-bit)
 *   DWORD = unsigned long (= ULONG, 32-bit on both 32-bit and 64-bit Windows)
 *   BOOL  = int           (32-bit, matching Windows BOOL)
 *   POINT and RECT are required by D3DKMT_MOVE_RECT in d3dkmdt.h.
 */
#ifndef _UINT_DEFINED
#define _UINT_DEFINED
typedef unsigned int UINT;
typedef UINT *PUINT;
#endif

#ifndef _BYTE_DEFINED
#define _BYTE_DEFINED
typedef unsigned char BYTE;
typedef BYTE *LPBYTE, *PBYTE;
#endif

#ifndef _DWORD_DEFINED
#define _DWORD_DEFINED
typedef ULONG DWORD;
typedef DWORD *LPDWORD, *PDWORD;
#endif

#ifndef _BOOL_DEFINED
#define _BOOL_DEFINED
typedef int BOOL;
#endif

#ifndef _POINT_DEFINED
#define _POINT_DEFINED
typedef struct tagPOINT { LONG x; LONG y; } POINT, *PPOINT, *LPPOINT;
#endif

#ifndef _RECT_DEFINED
#define _RECT_DEFINED
typedef struct tagRECT { LONG left; LONG top; LONG right; LONG bottom; } RECT, *PRECT, *LPRECT;
#endif

/*
 * WINAPI / APIENTRY are user-mode psdk types (defined in minwindef.h).
 * In kernel mode they are not pulled in by ntddk.h / wdm.h.  Define them
 * as __stdcall here (on x86) or empty (on x86-64, which has only one calling
 * convention).  These must be defined before including d3dkmddi.h which uses
 * APIENTRY in some of its callback typedefs.
 */
#ifndef WINAPI
#ifdef _M_AMD64
#define WINAPI
#else
#define WINAPI __stdcall
#endif
#endif

#ifndef APIENTRY
#define APIENTRY WINAPI
#endif

/* ---- WDDM DDI structures ------------------------------------------------- */
#include <d3dkmddi.h>

/* ---- Debug helpers ------------------------------------------------------- */
#define NDEBUG
#include <debug.h>

/* =========================================================================
 * WDDM miniport interface types required by softgpu
 *
 * These types are normally in dispmprt.h (Windows WDK) which is not yet
 * present in the ReactOS SDK.  They are also defined in the dxgkrnl-internal
 * header dxgkrnl_private.h / pnp.h.  We reproduce the necessary subset here
 * so softgpu.sys can compile without depending on dxgkrnl internal headers.
 *
 * The layouts must match dxgkrnl_private.h exactly; any divergence will cause
 * ABI mismatches at runtime.
 * ========================================================================= */

/* --- Child device types -------------------------------------------------- */

typedef enum _DXGK_CHILD_DEVICE_TYPE
{
    TypeUninitialized = 0,
    TypeVideoOutput   = 1,
    TypeOther         = 2
} DXGK_CHILD_DEVICE_TYPE;

typedef struct _DXGK_CHILD_CAPABILITIES
{
    union
    {
        struct
        {
            UINT HpdAwareness : 2;
            UINT AcpiUid      : 8;
            UINT Reserved     : 22;
        } Type;
        UINT Value;
    };
    ULONG TypeSpecific[4];
} DXGK_CHILD_CAPABILITIES;

typedef struct _DXGK_CHILD_DESCRIPTOR
{
    DXGK_CHILD_DEVICE_TYPE  ChildDeviceType;
    DXGK_CHILD_CAPABILITIES ChildCapabilities;
    ULONG                   AcpiUid;
    ULONG                   ChildUid;
} DXGK_CHILD_DESCRIPTOR, *PDXGK_CHILD_DESCRIPTOR;

typedef enum _DXGK_CHILD_STATUS_TYPE
{
    StatusUninitialized = 0,
    StatusConnection    = 1,
    StatusRotation      = 2
} DXGK_CHILD_STATUS_TYPE;

typedef struct _DXGK_CHILD_STATUS
{
    DXGK_CHILD_STATUS_TYPE  Type;
    ULONG                   ChildUid;
    union
    {
        struct { BOOLEAN Connected; } HotPlug;
        struct { UCHAR   Angle;    } Rotation;
    };
} DXGK_CHILD_STATUS, *PDXGK_CHILD_STATUS;

typedef struct _DXGK_DEVICE_DESCRIPTOR
{
    ULONG  DescriptorOffset;
    ULONG  DescriptorLength;
    PVOID  DescriptorBuffer;
} DXGK_DEVICE_DESCRIPTOR, *PDXGK_DEVICE_DESCRIPTOR;

/* --- DXGK_DEVICE_INFO ----------------------------------------------------- */

typedef struct _DXGK_DEVICE_INFO
{
    PDEVICE_OBJECT       PhysicalDeviceObject;
    PUNICODE_STRING      MiniportRegistryPath;
    PHYSICAL_ADDRESS     SegmentAperture;
    ULONGLONG            SegmentFrameBufferLength;
    PCM_RESOURCE_LIST    TranslatedResourceList;
    ULONG                SystemIoBusNumber;
    INTERFACE_TYPE       AdapterInterfaceType;
    ULONG                BusInterruptLevel;
    ULONG                BusInterruptVector;
    KINTERRUPT_MODE      InterruptMode;
    ULONG                DmaAddressWidth;
    ULONG                AgpApertureBase;
    ULONG                AgpApertureSize;
} DXGK_DEVICE_INFO, *PDXGK_DEVICE_INFO;

/* --- DXGK_START_INFO ------------------------------------------------------ */

typedef struct _DXGK_START_INFO
{
    ULONG              RequiredDxgkVtableVersion;
    PDXGK_DEVICE_INFO  DxgkDeviceInfo;
} DXGK_START_INFO, *PDXGK_START_INFO;

/* --- DxgkCb* callback typedefs ------------------------------------------- */

typedef NTSTATUS
(APIENTRY *PDXGKCB_NOTIFY_INTERRUPT)(
    _In_ HANDLE DeviceHandle,
    _In_ PVOID  NotifyInterruptData);

typedef NTSTATUS
(APIENTRY *PDXGKCB_NOTIFY_DPC)(
    _In_ HANDLE DeviceHandle);

typedef NTSTATUS
(APIENTRY *PDXGKCB_GET_DEVICE_INFORMATION)(
    _In_  HANDLE             DeviceHandle,
    _Out_ PDXGK_DEVICE_INFO  DeviceInformation);

typedef NTSTATUS
(APIENTRY *PDXGKCB_INDICATE_CHILD_STATUS)(
    _In_ HANDLE             DeviceHandle,
    _In_ PDXGK_CHILD_STATUS ChildStatus);

typedef NTSTATUS
(APIENTRY *PDXGKCB_SYNCHRONIZE_EXECUTION)(
    _In_  HANDLE                 DeviceHandle,
    _In_  PKSYNCHRONIZE_ROUTINE  SynchronizeRoutine,
    _In_  PVOID                  Context,
    _In_  ULONG                  MessageNumber,
    _Out_ PBOOLEAN               ReturnValue);

typedef NTSTATUS
(APIENTRY *PDXGKCB_ALLOCATE_CONTIGUOUS_MEMORY)(
    _In_    HANDLE  DeviceHandle,
    _Inout_ PVOID   AllocContiguousMemory);

typedef NTSTATUS
(APIENTRY *PDXGKCB_FREE_CONTIGUOUS_MEMORY)(
    _In_ HANDLE  DeviceHandle,
    _In_ PVOID   FreeContiguousMemory);

typedef NTSTATUS
(APIENTRY *PDXGKCB_MAP_PHYSICAL_MEMORY)(
    _In_    HANDLE  DeviceHandle,
    _Inout_ PVOID   MapPhysicalMemory);

typedef NTSTATUS
(APIENTRY *PDXGKCB_UNMAP_PHYSICAL_MEMORY)(
    _In_ HANDLE  DeviceHandle,
    _In_ PVOID   UnmapPhysicalMemory);

typedef NTSTATUS
(APIENTRY *PDXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP)(
    _In_  HANDLE  DeviceHandle,
    _Out_ PVOID   DisplayInformation);

/* --- DXGK_INTERFACE ------------------------------------------------------- */

typedef struct _DXGK_INTERFACE
{
    ULONG                                 Size;
    ULONG                                 Version;
    HANDLE                                DeviceHandle;
    PDXGKCB_NOTIFY_INTERRUPT              DxgkCbNotifyInterrupt;
    PDXGKCB_NOTIFY_DPC                    DxgkCbNotifyDpc;
    PDXGKCB_GET_DEVICE_INFORMATION        DxgkCbGetDeviceInformation;
    PDXGKCB_INDICATE_CHILD_STATUS         DxgkCbIndicateChildStatus;
    PDXGKCB_SYNCHRONIZE_EXECUTION         DxgkCbSynchronizeExecution;
    PDXGKCB_ALLOCATE_CONTIGUOUS_MEMORY    DxgkCbAllocateContiguousMemory;
    PDXGKCB_FREE_CONTIGUOUS_MEMORY        DxgkCbFreeContiguousMemory;
    PDXGKCB_MAP_PHYSICAL_MEMORY           DxgkCbMapPhysicalMemory;
    PDXGKCB_UNMAP_PHYSICAL_MEMORY         DxgkCbUnmapPhysicalMemory;
    PDXGKCB_ACQUIRE_POST_DISPLAY_OWNERSHIP DxgkCbAcquirePostDisplayOwnership;
} DXGK_INTERFACE, *PDXGK_INTERFACE;

/* --- DRIVER_INITIALIZATION_DATA ------------------------------------------ */

/* Forward-declare the miniport DDI pointer types used in the table. */
typedef NTSTATUS (APIENTRY *PDXGKDDI_ADD_DEVICE)(
    _In_  PDEVICE_OBJECT  PhysicalDeviceObject,
    _Out_ PVOID          *MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_START_DEVICE)(
    _In_  PVOID             MiniportDeviceContext,
    _In_  PDXGK_START_INFO  DxgkStartInfo,
    _In_  PDXGK_INTERFACE   DxgkInterface,
    _Out_ PULONG            NumberOfVideoPresentSources,
    _Out_ PULONG            NumberOfChildren);

typedef NTSTATUS (APIENTRY *PDXGKDDI_STOP_DEVICE)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_REMOVE_DEVICE)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_DISPATCH_IO_REQUEST)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ ULONG  VidPnSourceId,
    _In_ PVOID  VideoRequestPacket);     /* VIDEO_REQUEST_PACKET* — from video.h */

typedef BOOLEAN (APIENTRY *PDXGKDDI_INTERRUPT_ROUTINE)(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG MessageNumber);

typedef VOID (APIENTRY *PDXGKDDI_DPC_ROUTINE)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_QUERY_CHILD_RELATIONS)(
    _In_  PVOID                  MiniportDeviceContext,
    _Out_ PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_  ULONG                  ChildRelationsSize);

typedef NTSTATUS (APIENTRY *PDXGKDDI_QUERY_CHILD_STATUS)(
    _In_    PVOID              MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly);

typedef NTSTATUS (APIENTRY *PDXGKDDI_QUERY_DEVICE_DESCRIPTOR)(
    _In_    PVOID                  MiniportDeviceContext,
    _In_    ULONG                  ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor);

typedef NTSTATUS (APIENTRY *PDXGKDDI_SET_POWER_STATE)(
    _In_ PVOID             MiniportDeviceContext,
    _In_ ULONG             DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION      ActionType);

typedef NTSTATUS (APIENTRY *PDXGKDDI_NOTIFY_ACPI_EVENT)(
    _In_      PVOID  MiniportDeviceContext,
    _In_      ULONG  EventType,
    _In_      ULONG  Event,
    _In_      PVOID  Argument,
    _Out_opt_ PULONG AcpiFlags);

typedef VOID (APIENTRY *PDXGKDDI_RESET_DEVICE)(
    _In_ PVOID MiniportDeviceContext);

typedef VOID (APIENTRY *PDXGKDDI_UNLOAD)(VOID);

typedef NTSTATUS (APIENTRY *PDXGKDDI_QUERY_INTERFACE)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ PVOID  QueryInterface);         /* QUERY_INTERFACE* — from video.h */

typedef NTSTATUS (APIENTRY *PDXGKDDI_CONTROL_ETW_LOGGING)(
    _In_ BOOLEAN Enable,
    _In_ ULONG   Flags,
    _In_ UCHAR   Level);

typedef NTSTATUS (APIENTRY *PDXGKDDI_QUERY_ADAPTER_INFO)(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO    *QueryAdapterInfo);

typedef NTSTATUS (APIENTRY *PDXGKDDI_CREATE_DEVICE)(
    _In_    PVOID                 MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEDEVICE CreateDevice);

typedef NTSTATUS (APIENTRY *PDXGKDDI_CREATE_ALLOCATION)(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEALLOCATION  CreateAllocation);

typedef NTSTATUS (APIENTRY *PDXGKDDI_DESTROY_ALLOCATION)(
    _In_ PVOID                               MiniportDeviceContext,
    _In_ CONST DXGKARG_DESTROYALLOCATION    *DestroyAllocation);

typedef NTSTATUS (APIENTRY *PDXGKDDI_DESCRIBE_ALLOCATION)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_DESCRIBEALLOCATION DescribeAllocation);

typedef NTSTATUS (APIENTRY *PDXGKDDI_GET_STDALLOC_UPDATEFLAGS)(
    _In_    PVOID  MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA StandardAllocationDriverData);

typedef NTSTATUS (APIENTRY *PDXGKDDI_ACQUIRE_SWIZZLING_RANGE)(
    _In_    PVOID                          MiniportDeviceContext,
    _Inout_ PDXGKARG_ACQUIRESWIZZLINGRANGE AcquireSwizzlingRange);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RELEASE_SWIZZLING_RANGE)(
    _In_ PVOID                           MiniportDeviceContext,
    _In_ PDXGKARG_RELEASESWIZZLINGRANGE  ReleaseSwizzlingRange);

typedef NTSTATUS (APIENTRY *PDXGKDDI_PATCH)(
    _In_ PVOID                  MiniportDeviceContext,
    _In_ CONST DXGKARG_PATCH   *Patch);

typedef NTSTATUS (APIENTRY *PDXGKDDI_SUBMIT_COMMAND)(
    _In_ PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand);

typedef NTSTATUS (APIENTRY *PDXGKDDI_PREEMPT_COMMAND)(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *PreemptCommand);

typedef NTSTATUS (APIENTRY *PDXGKDDI_BUILD_PAGING_BUFFER)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER  BuildPagingBuffer);

typedef NTSTATUS (APIENTRY *PDXGKDDI_SET_PALETTE)(
    _In_ PVOID                   MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPALETTE *SetPalette);

typedef NTSTATUS (APIENTRY *PDXGKDDI_SET_POINTER_POSITION)(
    _In_ PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition);

typedef NTSTATUS (APIENTRY *PDXGKDDI_SET_POINTER_SHAPE)(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RESETFROMTIMEOUT)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RESTARTFROMTIMEOUT)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_ESCAPE)(
    _In_ PVOID                 MiniportDeviceContext,
    _In_ CONST DXGKARG_ESCAPE *Escape);

typedef NTSTATUS (APIENTRY *PDXGKDDI_COLLECT_DB_ENGINE_INFO)(
    _In_  PVOID                    MiniportDeviceContext,
    _Out_ PDXGKARG_COLLECTDBGINFO  CollectDbEngineInfo);

typedef NTSTATUS (APIENTRY *PDXGKDDI_QUERY_CURRENT_FENCE)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYCURRENTFENCE  CurrentFence);

typedef NTSTATUS (APIENTRY *PDXGKDDI_IS_SUPPORTED_VIDPN)(
    _In_    PVOID                     MiniportDeviceContext,
    _Inout_ PDXGKARG_ISSUPPORTEDVIDPN IsSupportedVidPn);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN)(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *RecommendFunctionalVidPn);

typedef NTSTATUS (APIENTRY *PDXGKDDI_ENUM_VIDPN_COFUNC_MODALITY)(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *EnumCofuncModality);

typedef NTSTATUS (APIENTRY *PDXGKDDI_SET_VIDPN_SOURCE_ADDRESS)(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress);

typedef NTSTATUS (APIENTRY *PDXGKDDI_SET_VIDPN_SOURCE_VISIBILITY)(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility);

typedef NTSTATUS (APIENTRY *PDXGKDDI_COMMIT_VIDPN)(
    _In_ PVOID                      MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN *CommitVidPn);

typedef NTSTATUS (APIENTRY *PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH)(
    _In_ PVOID                                      MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RECOMMEND_MONITORMODES)(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *RecommendMonitorModes);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RECOMMEND_VIDPN_TOPOLOGY)(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *RecommendVidPnTopology);

typedef NTSTATUS (APIENTRY *PDXGKDDI_GET_SCAN_LINE)(
    _In_    PVOID                 MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSCANLINE  GetScanLine);

typedef NTSTATUS (APIENTRY *PDXGKDDI_STOP_CAPTURE)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ PVOID  StopCapture);   /* PDXGKARG_STOPCAPTURE — not used by softgpu */

typedef NTSTATUS (APIENTRY *PDXGKDDI_CONTROL_INTERRUPT)(
    _In_ PVOID                     MiniportDeviceContext,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN                   EnableInterrupt);

typedef NTSTATUS (APIENTRY *PDXGKDDI_CREATE_OVERLAY)(
    _In_    PVOID   MiniportDeviceContext,
    _Inout_ PVOID   CreateOverlay);   /* PDXGKARG_CREATEOVERLAY */

typedef NTSTATUS (APIENTRY *PDXGKDDI_DESTROY_DEVICE)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_OPEN_ALLOCATION)(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_OPENALLOCATION *OpenAllocation);

typedef NTSTATUS (APIENTRY *PDXGKDDI_CLOSE_ALLOCATION)(
    _In_ PVOID                          MiniportDeviceContext,
    _In_ CONST DXGKARG_CLOSEALLOCATION *CloseAllocation);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RENDER)(
    _In_    PVOID            MiniportDeviceContext,
    _Inout_ PDXGKARG_RENDER  Render);

typedef NTSTATUS (APIENTRY *PDXGKDDI_PRESENT)(
    _In_    PVOID             MiniportDeviceContext,
    _Inout_ PDXGKARG_PRESENT  Present);

typedef NTSTATUS (APIENTRY *PDXGKDDI_UPDATE_OVERLAY)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ PVOID  UpdateOverlay);   /* PDXGKARG_UPDATEOVERLAY */

typedef NTSTATUS (APIENTRY *PDXGKDDI_FLIP_OVERLAY)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ PVOID  FlipOverlay);     /* PDXGKARG_FLIPOVERLAY */

typedef NTSTATUS (APIENTRY *PDXGKDDI_DESTROY_OVERLAY)(
    _In_ PVOID  MiniportDeviceContext,
    _In_ PVOID  DestroyOverlay);  /* PDXGKARG_DESTROYOVERLAY */

typedef NTSTATUS (APIENTRY *PDXGKDDI_CREATE_CONTEXT)(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATECONTEXT  CreateContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_DESTROY_CONTEXT)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_LINK_DEVICE)(
    _In_    CONST PDEVICE_OBJECT  PhysicalDeviceObject,
    _In_    CONST PVOID           MiniportDeviceContext,
    _Inout_ PVOID                 LinkedDevice);

typedef NTSTATUS (APIENTRY *PDXGKDDI_SET_DISPLAY_PRIVATE_DRIVER_FORMAT)(
    _In_ PVOID                                       MiniportDeviceContext,
    _In_ CONST DXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT *SetDisplayPrivateDriverFormat);

/*
 * DRIVER_INITIALIZATION_DATA
 *
 * Must match the definition in dxgkrnl_private.h (same layout, same
 * pointer-type sizes).
 */
typedef struct _DRIVER_INITIALIZATION_DATA
{
    ULONG                                       Version;
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
    PDXGKDDI_CREATE_DEVICE                      DxgkDdiCreateDevice;
    PDXGKDDI_CREATE_ALLOCATION                  DxgkDdiCreateAllocation;
    PDXGKDDI_DESTROY_ALLOCATION                 DxgkDdiDestroyAllocation;
    PDXGKDDI_DESCRIBE_ALLOCATION                DxgkDdiDescribeAllocation;
    PDXGKDDI_GET_STDALLOC_UPDATEFLAGS           DxgkDdiGetStandardAllocationDriverData;
    PDXGKDDI_ACQUIRE_SWIZZLING_RANGE            DxgkDdiAcquireSwizzlingRange;
    PDXGKDDI_RELEASE_SWIZZLING_RANGE            DxgkDdiReleaseSwizzlingRange;
    PDXGKDDI_PATCH                              DxgkDdiPatch;
    PDXGKDDI_SUBMIT_COMMAND                     DxgkDdiSubmitCommand;
    PDXGKDDI_PREEMPT_COMMAND                    DxgkDdiPreemptCommand;
    PDXGKDDI_BUILD_PAGING_BUFFER                DxgkDdiBuildPagingBuffer;
    PDXGKDDI_SET_PALETTE                        DxgkDdiSetPalette;
    PDXGKDDI_SET_POINTER_POSITION               DxgkDdiSetPointerPosition;
    PDXGKDDI_SET_POINTER_SHAPE                  DxgkDdiSetPointerShape;
    PDXGKDDI_RESETFROMTIMEOUT                   DxgkDdiResetFromTimeout;
    PDXGKDDI_RESTARTFROMTIMEOUT                 DxgkDdiRestartFromTimeout;
    PDXGKDDI_ESCAPE                             DxgkDdiEscape;
    PDXGKDDI_COLLECT_DB_ENGINE_INFO             DxgkDdiCollectDbEngineInfo;
    PDXGKDDI_QUERY_CURRENT_FENCE                DxgkDdiQueryCurrentFence;
    PDXGKDDI_IS_SUPPORTED_VIDPN                 DxgkDdiIsSupportedVidPn;
    PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN         DxgkDdiRecommendFunctionalVidPn;
    PDXGKDDI_ENUM_VIDPN_COFUNC_MODALITY         DxgkDdiEnumVidPnCofuncModality;
    PDXGKDDI_SET_VIDPN_SOURCE_ADDRESS           DxgkDdiSetVidPnSourceAddress;
    PDXGKDDI_SET_VIDPN_SOURCE_VISIBILITY        DxgkDdiSetVidPnSourceVisibility;
    PDXGKDDI_COMMIT_VIDPN                       DxgkDdiCommitVidPn;
    PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH   DxgkDdiUpdateActiveVidPnPresentPath;
    PDXGKDDI_RECOMMEND_MONITORMODES             DxgkDdiRecommendMonitorModes;
    PDXGKDDI_RECOMMEND_VIDPN_TOPOLOGY           DxgkDdiRecommendVidPnTopology;
    PDXGKDDI_GET_SCAN_LINE                      DxgkDdiGetScanLine;
    PDXGKDDI_STOP_CAPTURE                       DxgkDdiStopCapture;
    PDXGKDDI_CONTROL_INTERRUPT                  DxgkDdiControlInterrupt;
    PDXGKDDI_CREATE_OVERLAY                     DxgkDdiCreateOverlay;
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
} DRIVER_INITIALIZATION_DATA, *PDRIVER_INITIALIZATION_DATA;

/* DxgkInitialize prototype — resolves to the export from dxgkrnl.sys */
NTSTATUS
APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData);

/* =========================================================================
 * VidPN types
 *
 * In this ReactOS implementation D3DKMDT_HVIDPN is an opaque handle that
 * resolves to a pointer to DXGKRNL_VIDPN (defined in dxgkrnl/vidpn.h).
 * softgpu does not manipulate VidPN objects directly — it just sets fields
 * on pinned mode structures returned by dxgkrnl.
 *
 * The structures below are a simplified local view.  The actual VidPN object
 * lives inside dxgkrnl; we cast the handle and access the fields that
 * dxgkrnl exposes.
 * ========================================================================= */

/*
 * Maximum modes per VidPN source supported by this miniport.
 * dxgkrnl may cap this further in its own DXGKRNL_VIDPN structure.
 */
#define SOFTGPU_MAX_MODES   6

/* =========================================================================
 * SOFTGPU_DEVICE — per-adapter miniport context
 * ========================================================================= */

#define SOFTGPU_DEVICE_MAGIC    0x53474455UL    /* 'UDGS' — SoftGpU Device  */
#define SOFTGPU_POOL_TAG        'uGfS'          /* 'SfGu' reversed          */

/* Segment 1 is the only segment softgpu exposes (segment IDs are 1-based). */
#define SOFTGPU_SEGMENT_ID      1

/* Framebuffer size: 16 MB (sufficient for 1920x1080x32bpp = ~8 MB) */
#define SOFTGPU_FB_SIZE         (16 * 1024 * 1024)

typedef struct _SOFTGPU_DEVICE
{
    /* Sanity / validation marker */
    ULONG               Magic;

    /* Number of VidPN sources / child devices (both 1 for softgpu) */
    ULONG               NumSources;
    ULONG               NumChildren;

    /*
     * 16 MB write-combined contiguous framebuffer slab.
     * MmAllocateContiguousMemorySpecifyCache with MmWriteCombined.
     * FrameBufferPhys is the physical address for segment reporting.
     * FrameBuffer     is the kernel-virtual mapping (always valid).
     */
    PVOID               FrameBuffer;
    PHYSICAL_ADDRESS    FrameBufferPhys;
    SIZE_T              FrameBufferSize;

    /* Currently committed display mode */
    ULONG               Width;
    ULONG               Height;
    D3DDDIFORMAT        Format;

    /*
     * Fence tracking.
     *
     * CurrentFence  — fence ID of the last SubmitCommand call.
     * CompletedFence— fence ID of the last DPC completion.
     *
     * Both are protected by FenceLock (KSPIN_LOCK).
     * The DPC sets CompletedFence = CurrentFence inside the lock,
     * then calls DxgkCbNotifyInterrupt / DxgkCbNotifyDpc.
     */
    ULONG               CurrentFence;
    ULONG               CompletedFence;
    KSPIN_LOCK          FenceLock;

    /*
     * DPC object queued by SubmitCommand at DISPATCH_LEVEL.
     * The DPC fires SoftGpuDpcRoutine which notifies dxgkrnl of
     * fence completion.
     */
    KDPC                DpcObject;

    /*
     * dxgkrnl callback vtable.  Copied from the PDXGK_INTERFACE argument
     * to DxgkDdiStartDevice.
     */
    DXGK_INTERFACE      DxgkInterface;

} SOFTGPU_DEVICE, *PSOFTGPU_DEVICE;


/* =========================================================================
 * SOFTGPU_ALLOC — per-allocation miniport context
 *
 * Stored in DXGK_ALLOCATIONINFO.hAllocation for each allocation created
 * by DxgkDdiCreateAllocation.
 * ========================================================================= */

#define SOFTGPU_ALLOC_MAGIC     0x50554753UL    /* 'SGUP' */

typedef struct _SOFTGPU_ALLOC
{
    ULONG           Magic;          /* must equal SOFTGPU_ALLOC_MAGIC       */
    SIZE_T          Size;           /* allocation size in bytes             */
    ULONG           Width;          /* surface width in pixels (if 2D)      */
    ULONG           Height;         /* surface height in pixels (if 2D)     */
    ULONG           Pitch;          /* stride in bytes                      */
    D3DDDIFORMAT    Format;         /* surface pixel format                 */
} SOFTGPU_ALLOC, *PSOFTGPU_ALLOC;


/* =========================================================================
 * SOFTGPU_CONTEXT — per-context miniport context
 *
 * Stored in DXGKARG_CREATECONTEXT.hContext (out) for each context created
 * by DxgkDdiCreateContext.
 * ========================================================================= */

typedef struct _SOFTGPU_CONTEXT
{
    ULONG   Magic;
    ULONG   NodeOrdinal;
    ULONG   EngineAffinity;
} SOFTGPU_CONTEXT, *PSOFTGPU_CONTEXT;

#define SOFTGPU_CONTEXT_MAGIC   0x43504753UL    /* 'SGPC' */


/* =========================================================================
 * Function prototypes — softgpu.c (DriverEntry + core DDIs)
 * ========================================================================= */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

NTSTATUS
APIENTRY
SoftGpuDdiAddDevice(
    _In_  PDEVICE_OBJECT  PhysicalDeviceObject,
    _Out_ PVOID          *MiniportDeviceContext);

NTSTATUS
APIENTRY
SoftGpuDdiStartDevice(
    _In_  PVOID             MiniportDeviceContext,
    _In_  PDXGK_START_INFO  DxgkStartInfo,
    _In_  PDXGK_INTERFACE   DxgkInterface,
    _Out_ PULONG            NumberOfVideoPresentSources,
    _Out_ PULONG            NumberOfChildren);

NTSTATUS
APIENTRY
SoftGpuDdiStopDevice(
    _In_ PVOID MiniportDeviceContext);

NTSTATUS
APIENTRY
SoftGpuDdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext);

NTSTATUS
APIENTRY
SoftGpuDdiQueryAdapterInfo(
    _In_ PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO  *pQueryAdapterInfo);

NTSTATUS
APIENTRY
SoftGpuDdiCreateDevice(
    _In_    PVOID                 MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEDEVICE CreateDevice);

NTSTATUS
APIENTRY
SoftGpuDdiDestroyDevice(
    _In_ PVOID MiniportDeviceContext);

NTSTATUS
APIENTRY
SoftGpuDdiCreateAllocation(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEALLOCATION  CreateAllocation);

NTSTATUS
APIENTRY
SoftGpuDdiDestroyAllocation(
    _In_ PVOID                             MiniportDeviceContext,
    _In_ CONST DXGKARG_DESTROYALLOCATION  *DestroyAllocation);

NTSTATUS
APIENTRY
SoftGpuDdiQueryChildRelations(
    _In_  PVOID                  MiniportDeviceContext,
    _Out_ PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_  ULONG                  ChildRelationsSize);

NTSTATUS
APIENTRY
SoftGpuDdiQueryChildStatus(
    _In_    PVOID              MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly);

NTSTATUS
APIENTRY
SoftGpuDdiResetFromTimeout(
    _In_ PVOID MiniportDeviceContext);

NTSTATUS
APIENTRY
SoftGpuDdiRestartFromTimeout(
    _In_ PVOID MiniportDeviceContext);

VOID
APIENTRY
SoftGpuDdiResetDevice(
    _In_ PVOID MiniportDeviceContext);

NTSTATUS
APIENTRY
SoftGpuDdiSetPowerState(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType);

NTSTATUS
APIENTRY
SoftGpuDdiControlInterrupt(
    _In_ PVOID                     MiniportDeviceContext,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN                   EnableInterrupt);


/* =========================================================================
 * Function prototypes — dma.c
 * ========================================================================= */

VOID
NTAPI
SoftGpuDpcRoutine(
    _In_     PKDPC   Dpc,
    _In_opt_ PVOID   DeferredContext,
    _In_opt_ PVOID   SystemArgument1,
    _In_opt_ PVOID   SystemArgument2);

NTSTATUS
APIENTRY
SoftGpuDdiSubmitCommand(
    _In_ PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand);

NTSTATUS
APIENTRY
SoftGpuDdiPreemptCommand(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *PreemptCommand);

NTSTATUS
APIENTRY
SoftGpuDdiBuildPagingBuffer(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer);

NTSTATUS
APIENTRY
SoftGpuDdiQueryCurrentFence(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYCURRENTFENCE CurrentFence);

NTSTATUS
APIENTRY
SoftGpuDdiPatch(
    _In_ PVOID                    MiniportDeviceContext,
    _In_ CONST DXGKARG_PATCH     *Patch);

BOOLEAN
APIENTRY
SoftGpuDdiInterruptRoutine(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG MessageNumber);

VOID
APIENTRY
SoftGpuDdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext);

NTSTATUS
APIENTRY
SoftGpuDdiSetPointerPosition(
    _In_ PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition);

NTSTATUS
APIENTRY
SoftGpuDdiSetPointerShape(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape);

NTSTATUS
APIENTRY
SoftGpuDdiSetPalette(
    _In_ PVOID                   MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPALETTE *SetPalette);

NTSTATUS
APIENTRY
SoftGpuDdiGetScanLine(
    _In_    PVOID                 MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSCANLINE  GetScanLine);

NTSTATUS
APIENTRY
SoftGpuDdiCreateContext(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATECONTEXT  CreateContext);

NTSTATUS
APIENTRY
SoftGpuDdiDestroyContext(
    _In_ PVOID MiniportDeviceContext);


/* =========================================================================
 * Function prototypes — vidpn.c
 * ========================================================================= */

NTSTATUS
APIENTRY
SoftGpuDdiIsSupportedVidPn(
    _In_    PVOID                     MiniportDeviceContext,
    _Inout_ PDXGKARG_ISSUPPORTEDVIDPN IsSupportedVidPn);

NTSTATUS
APIENTRY
SoftGpuDdiRecommendFunctionalVidPn(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *RecommendFunctionalVidPn);

NTSTATUS
APIENTRY
SoftGpuDdiEnumVidPnCofuncModality(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *EnumCofuncModality);

NTSTATUS
APIENTRY
SoftGpuDdiSetVidPnSourceAddress(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress);

NTSTATUS
APIENTRY
SoftGpuDdiSetVidPnSourceVisibility(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility);

NTSTATUS
APIENTRY
SoftGpuDdiCommitVidPn(
    _In_ PVOID                      MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN *CommitVidPn);

NTSTATUS
APIENTRY
SoftGpuDdiUpdateActiveVidPnPresentPath(
    _In_ PVOID                                      MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath);

NTSTATUS
APIENTRY
SoftGpuDdiRecommendMonitorModes(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *RecommendMonitorModes);

NTSTATUS
APIENTRY
SoftGpuDdiRecommendVidPnTopology(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *RecommendVidPnTopology);

NTSTATUS
APIENTRY
SoftGpuDdiQueryDeviceDescriptor(
    _In_    PVOID                   MiniportDeviceContext,
    _In_    ULONG                   ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor);

/* EOF */
