/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Private header for softgpu.sys — WDDM 2.0 ABI miniport with a
 *              physical/software engine and no GPU MMU, targeting QEMU STD
 *              VGA (PCI VEN_1234&DEV_1111) or the firmware framebuffer.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Architecture notes
 * ==================
 * softgpu is a pure software device with no real GPU hardware or GPU MMU. It
 * allocates a mode-sized write-combined contiguous buffer from system RAM and
 * presents it to dxgkrnl as one CPU-visible physical aperture segment.
 *
 * ABI contract (CRITICAL)
 * =======================
 * softgpu and dxgkrnl compile against the same SDK <dispmprt.h> layout.  The
 * header may include newer ABI tails, while softgpu deliberately declares
 * DXGKDDI_INTERFACE_VERSION_WDDM2_0 (0x5023) in InitData.Version.  Keeping the
 * compile-time layout identical is mandatory, not cosmetic:
 *
 *   - dxgkrnl's DxgkInitialize() derives the readable WDDM 2.0 prefix from
 *     InitData.Version and copies exactly that prefix.  The compile-time table
 *     may be larger, but its newer zeroed tail is neither read nor advertised.
 *
 *   - dxgkrnl fills the DXGK_INTERFACE it passes to DxgkDdiStartDevice using the
 *     dispmprt.h layout (DxgkCbNotifyInterrupt at offset 0x80, NotifyDpc 0x88).
 *     softgpu's DPC calls those callbacks, so the layouts must match exactly.
 *
 * Earlier revisions hand-rolled a WDDM 1.0 subset of these structures.  That
 * only survived because the verified display path uses SetVidPnSourceAddress
 * flips, not the DMA-submit / interrupt-notify path.  The WDDM2 apitests DO
 * drive submission, so the hand-rolled layouts are removed in favour of the SDK
 * header that dxgkrnl itself uses.
 *
 * Fence tracking uses a KSPIN_LOCK (FenceLock) to guard CurrentFence and
 * CompletedFence across the DPC (DISPATCH_LEVEL) vs. PASSIVE_LEVEL boundary.
 */

#pragma once

/* ---- Minimum OS version (must precede the kernel headers) --------------- *
 * Mirror dxgkrnl_private.h.  The ReactOS build targets XP (0x502) globally;
 * the WDDM2 structures in dispmprt.h are gated on NTDDI_WIN7, so raise the
 * version both here and on the command line (see CMakeLists.txt). */
#undef  _WIN32_WINNT
#define _WIN32_WINNT  0x0601    /* Windows 7 */
#undef  WINVER
#define WINVER        0x0601
#undef  NTDDI_VERSION
#define NTDDI_VERSION 0x06010000 /* NTDDI_WIN7 */

/* ---- WDDM DDI interface version: WDDM 2.0 (Win10) ----------------------- *
 * CMakeLists.txt passes -DDXGKDDI_INTERFACE_VERSION=0x5023; default it here
 * defensively so the WDDM2 DRIVER_INITIALIZATION_DATA / DXGKARG_* fields are
 * visible even if the command-line define is ever dropped. */
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x5023 /* DXGKDDI_INTERFACE_VERSION_WDDM2_0 */
#endif

/*
 * Keep direct translation-unit probes deterministic even when they do not go
 * through this driver's CMake target.  The target overrides this with the
 * selected menuconfig level.
 */
#ifndef REACTOS_WDDM_TARGET_LEVEL
#define REACTOS_WDDM_TARGET_LEVEL 2000
#endif

/* ---- Kernel / WDM headers ---------------------------------------------- */
#include <ntddk.h>
#include <wdm.h>

/* ---- Windows base types (UINT/BYTE/DWORD/BOOL/POINT/RECT) needed by
 *      d3dukmdt.h.  Supplied by windef.h exactly as dxgkrnl_private.h does. */
#include <windef.h>

/* ---- WDDM public miniport interface ------------------------------------ *
 * dispmprt.h transitively includes d3dkmddi.h -> d3dkmdt.h -> d3dukmdt.h and
 * provides DRIVER_INITIALIZATION_DATA, DXGK_INTERFACE, DXGK_START_INFO, the
 * DXGK_CHILD_* / DXGK_DEVICE_* descriptors, DXGK_DRIVERCAPS, every DXGKARG_*
 * argument struct, the WDDM2 DDI typedefs and the DxgkInitialize prototype.
 * It is the same header dxgkrnl is built against, guaranteeing matching ABI. */
#include <dispmprt.h>

/* ---- Debug helpers ------------------------------------------------------- */
#define NDEBUG
#include <debug.h>

#include "gpuva_context_core.h"
#include "softgpu_2d_contract.h"

typedef struct _SOFTGPU_LOADER_FRAMEBUFFER
{
    LARGE_INTEGER FrameBufferBase;
    ULONG FrameBufferSize;
    ULONG HorizontalResolution;
    ULONG VerticalResolution;
    ULONG PixelsPerScanLine;
    ULONG PixelFormat;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG Reserved;
    ULONG Dpi;
} SOFTGPU_LOADER_FRAMEBUFFER, *PSOFTGPU_LOADER_FRAMEBUFFER;

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PSOFTGPU_LOADER_FRAMEBUFFER FrameBufferInfo);

/* =========================================================================
 * VidPN types
 *
 * D3DKMDT_HVIDPN is an opaque handle that dxgkrnl resolves internally.
 * softgpu does not manipulate VidPN objects directly — it only sets fields on
 * pinned mode structures returned by dxgkrnl.
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

#define SOFTGPU_DEFAULT_WIDTH           1024UL
#define SOFTGPU_DEFAULT_HEIGHT          768UL
#define SOFTGPU_DEFAULT_FORMAT          D3DDDIFMT_A8R8G8B8

/* GPU virtual-memory geometry this device declares: a 4-level radix of 9
 * index bits per level over 4 KB pages, i.e. a 48-bit address space. */
#define SOFTGPU_GPUVA_INDEX_BITS 9
#define SOFTGPU_GPUVA_LEVELS     4
#define SOFTGPU_GPUVA_BIT_COUNT  (12 + SOFTGPU_GPUVA_INDEX_BITS * SOFTGPU_GPUVA_LEVELS)
#define SOFTGPU_GPUVA_LIMIT      (1ULL << SOFTGPU_GPUVA_BIT_COUNT)

typedef enum _SOFTGPU_GPUVA_ACCESS
{
    SoftGpuGpuVaRead    = 0x1,
    SoftGpuGpuVaWrite   = 0x2,
    SoftGpuGpuVaExecute = 0x4
} SOFTGPU_GPUVA_ACCESS;

#define SOFTGPU_SUBMIT_RING_SIZE 1024

typedef struct _SOFTGPU_SUBMIT
{
    PHYSICAL_ADDRESS DmaPhys;
    ULONGLONG        DmaGpuVa;
    HANDLE           DxgkProcessHandle;
    ULONG            StartOffset;
    ULONG            EndOffset;
    ULONG            Fence;
    BOOLEAN          VirtualAddressing;
    BOOLEAN          NullRendering;
    SOFTGPU_GPUVA_ROOT Root;
} SOFTGPU_SUBMIT, *PSOFTGPU_SUBMIT;

typedef struct _SOFTGPU_DEVICE
{
    /* Sanity / validation marker */
    ULONG               Magic;

    /*
     * PnP identity supplied by dxgkrnl. Platform providers may validate the
     * bus/device contract, while the software engine remains device-neutral.
     */
    PDEVICE_OBJECT      PhysicalDeviceObject;

    /* Number of VidPN sources / child devices (both 1 for softgpu) */
    ULONG               NumSources;
    ULONG               NumChildren;

    /*
     * Mode-sized write-combined contiguous framebuffer segment.
     * MmAllocateContiguousMemorySpecifyCache with MmWriteCombined.
     * FrameBufferPhys is the physical address for segment reporting.
     * FrameBuffer     is the kernel-virtual mapping (always valid).
     */
    PVOID               FrameBuffer;
    PHYSICAL_ADDRESS    FrameBufferPhys;
    SIZE_T              FrameBufferSize;

    /*
     * Fixed firmware scanout. Allocations live in FrameBuffer above; the
     * current primary is copied here because this software miniport has no
     * display-engine register capable of flipping the firmware GOP base.
     */
    PVOID               Scanout;
    PHYSICAL_ADDRESS    ScanoutPhys;
    SIZE_T              ScanoutSize;
    ULONG               ScanoutPitch;
    KSPIN_LOCK          ScanoutLock;
    KMUTEX              ScanoutMutex;
    EX_RUNDOWN_REF      ScanoutRundown;
    WORK_QUEUE_ITEM     ScanoutWorkItem;
    BOOLEAN             ScanoutRundownCompleted;
    volatile LONG       ScanoutWorkQueued;
    ULONG               ScanoutGeneration;
    ULONG               ScanoutPresentedGeneration;
    ULONGLONG           CurrentPrimaryOffset;
    ULONG               CurrentPrimaryPitch;
    ULONG               CurrentPrimaryWidth;
    ULONG               CurrentPrimaryHeight;
    BOOLEAN             CurrentPrimaryValid;
    BOOLEAN             ScanoutVisible;
    BOOLEAN             TimingActive;

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
    /* Last value reported through DxgkCbNotifyInterrupt.  A completion
     * interrupt is raised only when the completed fence advances, so the
     * refresh-timer kick cannot replay a stale completion. */
    ULONG               NotifiedFence;
    KSPIN_LOCK          FenceLock;

    /*
     * DPC object queued by SubmitCommand at DISPATCH_LEVEL.
     * The DPC fires SoftGpuDpcRoutine which notifies dxgkrnl of
     * fence completion.
     */
    KDPC                DpcObject;
    volatile LONG       Stopped;
    BOOLEAN             DpcInitialized;

    KTIMER              VsyncTimer;
    KDPC                VsyncDpc;
    volatile LONG       VsyncPhaseEnabled;
    volatile LONG       VsyncEnabled;
    BOOLEAN             VsyncTimerInitialized;

    SOFTGPU_SUBMIT      SubmitRing[SOFTGPU_SUBMIT_RING_SIZE];
    ULONG               SubmitRingHead;
    ULONG               SubmitRingTail;
    LONG                EngineActive;

    /*
     * dxgkrnl callback vtable.  Copied from the PDXGK_INTERFACE argument
     * to DxgkDdiStartDevice (full WDDM2 dispmprt.h layout).
     */
    DXGK_INTERFACE      DxgkInterface;
#if (REACTOS_WDDM_TARGET_LEVEL >= 3000)
    BOOLEAN             KmdSignalCpuEventEnabled;
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
    /*
     * A successful feature-interface query owns one reference until the
     * consumer calls InterfaceDereference. StopDevice closes the admission
     * gate under FenceLock and waits for the count to return to zero before
     * clearing the callback table.
     */
    volatile LONG       FeatureInterfaceQueriesOpen;
    volatile LONG       FeatureNegotiationActive;
    volatile LONG       FeatureInterfaceReferences;
    KEVENT              FeatureInterfaceZeroEvent;
#endif

} SOFTGPU_DEVICE, *PSOFTGPU_DEVICE;

typedef struct _SOFTGPU_PLATFORM_CONFIG
{
    ULONG               Width;
    ULONG               Height;
    D3DDDIFORMAT        Format;
    PHYSICAL_ADDRESS    ScanoutPhysicalAddress;
    ULONG               ScanoutPitch;
    ULONGLONG           ScanoutSize;
} SOFTGPU_PLATFORM_CONFIG, *PSOFTGPU_PLATFORM_CONFIG;

/*
 * The generic software engine links exactly one platform provider. The root
 * fallback and hardware-bound drivers supply different implementations, so
 * PCI/vendor policy never enters the shared engine.
 */
NTSTATUS
SoftGpuPlatformValidatePdo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject);

NTSTATUS
SoftGpuPlatformQueryStart(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config);

VOID
SoftGpuPlatformFillNodeMetadata(
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata);

BOOLEAN
SoftGpuDecodeLoaderGop(
    _In_ const SOFTGPU_LOADER_FRAMEBUFFER *FrameBuffer,
    _Out_ PULONG Pitch,
    _Out_ PULONGLONG VisibleLength);

BOOLEAN
SoftGpuValidatePostDisplayInfo(
    _In_ const DXGK_DISPLAY_INFORMATION *DisplayInfo,
    _Out_ PULONGLONG VisibleLength);

NTSTATUS
SoftGpuAcquirePostDisplay(
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo,
    _Out_ PULONGLONG VisibleLength);

VOID
SoftGpuScanoutInitializeDevice(
    _Inout_ PSOFTGPU_DEVICE Device);

NTSTATUS
SoftGpuScanoutStart(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_PLATFORM_CONFIG *Config);

VOID
SoftGpuScanoutStop(
    _Inout_ PSOFTGPU_DEVICE Device);


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
 * SOFTGPU_OPENALLOC — per-device open binding for a shared allocation
 *
 * Returned in DXGK_OPENALLOCATIONINFO.hDeviceSpecificAllocation by
 * DxgkDdiOpenAllocation and freed by DxgkDdiCloseAllocation.
 * ========================================================================= */

#define SOFTGPU_OPENALLOC_MAGIC 0x4F504753UL    /* 'SGPO' */

typedef struct _SOFTGPU_OPENALLOC
{
    ULONG           Magic;          /* must equal SOFTGPU_OPENALLOC_MAGIC   */
    struct _SOFTGPU_KMD_DEVICE *Device;
    D3DKMT_HANDLE   hAllocation;    /* dxgkrnl allocation handle (in)       */
    SIZE_T          Size;           /* declared linear surface size         */
    ULONG           Width;
    ULONG           Height;
    ULONG           Pitch;
    D3DDDIFORMAT    Format;
} SOFTGPU_OPENALLOC, *PSOFTGPU_OPENALLOC;


/* =========================================================================
 * SOFTGPU_CONTEXT — per-context miniport context
 *
 * Stored in DXGKARG_CREATECONTEXT.hContext (out) for each context created
 * by DxgkDdiCreateContext.
 * ========================================================================= */

typedef struct _SOFTGPU_CONTEXT
{
    ULONG                     Magic;
    ULONG                     NodeOrdinal;
    ULONG                     EngineAffinity;
    struct _SOFTGPU_KMD_DEVICE *Device;
    struct _SOFTGPU_PROCESS   *Process;
    SOFTGPU_GPUVA_ROOT         Root;
} SOFTGPU_CONTEXT, *PSOFTGPU_CONTEXT;

#define SOFTGPU_CONTEXT_MAGIC   0x43504753UL    /* 'SGPC' */


/* =========================================================================
 * SOFTGPU_PROCESS — per-process miniport context (WDDM 2.0 GPU VA model)
 *
 * Returned as DXGKARG_CREATEPROCESS.hKmdProcess from DxgkDdiCreateProcess and
 * handed back verbatim to DxgkDdiDestroyProcess. It owns the root used when
 * non-MultiEngineAware submission exposes a device rather than a context.
 * ========================================================================= */

#define SOFTGPU_PROCESS_MAGIC   0x32475053UL    /* 'SPG2' */
#define SOFTGPU_WDDM2_POOL_TAG  '2GfS'          /* 'SfG2' reversed          */

typedef struct _SOFTGPU_PROCESS
{
    ULONG               Magic;        /* must equal SOFTGPU_PROCESS_MAGIC   */
    HANDLE              hDxgkProcess; /* opaque dxgkrnl process handle (in) */
    PSOFTGPU_DEVICE     Adapter;
    /*
     * Non-MultiEngineAware SubmitCommand receives hDevice rather than
     * hContext. All devices and contexts in one process use this same GPUVA
     * root, while virtual submissions take the more specific context copy.
     */
    SOFTGPU_GPUVA_ROOT  Root;
} SOFTGPU_PROCESS, *PSOFTGPU_PROCESS;


/* =========================================================================
 * SOFTGPU_KMD_DEVICE — opaque per-DxgkDdiCreateDevice handle
 *
 * The wrapper keeps hDevice unique while associating ordinary physical
 * submissions with their process root. It is destroyed only after dxgkrnl has
 * drained and destroyed every context belonging to the device.
 * ========================================================================= */

#define SOFTGPU_KMD_DEVICE_MAGIC 0x444B4753UL    /* 'SGKD' */

typedef struct _SOFTGPU_KMD_DEVICE
{
    ULONG               Magic;
    PSOFTGPU_DEVICE     Adapter;
    PSOFTGPU_PROCESS    Process;
} SOFTGPU_KMD_DEVICE, *PSOFTGPU_KMD_DEVICE;

#if (REACTOS_WDDM_TARGET_LEVEL >= 3000)
#define SOFTGPU_CPU_EVENT_MAGIC 0x45434753UL /* 'SGCE' */

typedef struct _SOFTGPU_CPU_EVENT
{
    ULONG               Magic;
    PSOFTGPU_DEVICE     Adapter;
    HANDLE              hDxgCpuEvent;
    volatile LONG       Destroying;
    EX_RUNDOWN_REF      Rundown;
    KSPIN_LOCK          UsageLock;
    UINT                Usage[8];
} SOFTGPU_CPU_EVENT, *PSOFTGPU_CPU_EVENT;
#endif


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

#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
NTSTATUS
APIENTRY
SoftGpuDdiQueryInterface(
    _In_ PVOID MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface);
#endif

NTSTATUS
APIENTRY
SoftGpuDdiQueryAdapterInfo(
    _In_ PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO  *pQueryAdapterInfo);

NTSTATUS
APIENTRY
SoftGpuDdiGetNodeMetadata(
    _In_ PVOID MiniportDeviceContext,
    _In_ UINT NodeOrdinalAndAdapterIndex,
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata);

NTSTATUS
APIENTRY
SoftGpuDdiCreateDevice(
    _In_    PVOID                 MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEDEVICE CreateDevice);

NTSTATUS
APIENTRY
SoftGpuDdiDestroyDevice(
    _In_ PVOID MiniportDeviceContext);

#if (REACTOS_WDDM_TARGET_LEVEL >= 3000)
NTSTATUS
APIENTRY
SoftGpuDdiCreateCpuEvent(
    _In_ HANDLE MiniportDeviceContext,
    INOUT_PDXGKARG_CREATECPUEVENT Args);

NTSTATUS
APIENTRY
SoftGpuDdiDestroyCpuEvent(
    _In_ HANDLE MiniportDeviceContext,
    _In_ HANDLE KmdCpuEvent);

NTSTATUS
APIENTRY
SoftGpuDdiEscape(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_ESCAPE *Escape);
#endif

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
SoftGpuDdiGetStandardAllocationDriverData(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA
        GetStandardAllocationDriverData);

NTSTATUS
APIENTRY
SoftGpuDdiRender(
    _In_    PVOID           hContext,
    _Inout_ DXGKARG_RENDER *pRender);

NTSTATUS
APIENTRY
SoftGpuDdiOpenAllocation(
    _In_ PVOID                          hDevice,
    _In_ CONST DXGKARG_OPENALLOCATION  *OpenAllocation);

NTSTATUS
APIENTRY
SoftGpuDdiCloseAllocation(
    _In_ PVOID                          hDevice,
    _In_ CONST DXGKARG_CLOSEALLOCATION *CloseAllocation);

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

#if (REACTOS_WDDM_TARGET_LEVEL >= 1300)
NTSTATUS
APIENTRY
SoftGpuDdiControlInterrupt2(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_CONTROLINTERRUPT2 InterruptControl);
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2600)
NTSTATUS
APIENTRY
SoftGpuDdiCollectDiagnosticInfo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    INOUT_PDXGKARG_COLLECTDIAGNOSTICINFO CollectDiagnosticInfo);
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2700)
NTSTATUS
APIENTRY
SoftGpuDdiControlInterrupt3(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_CONTROLINTERRUPT3 *InterruptControl);
#endif


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

VOID
NTAPI
SoftGpuVsyncDpcRoutine(
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
SoftGpuDdiPresent(
    _In_    PVOID            hContext,
    _Inout_ DXGKARG_PRESENT *pPresent);

BOOLEAN
SoftGpuValidateGpuVaRange(
    _In_ struct _SOFTGPU_DEVICE *Device,
    _In_ ULONGLONG RootPhysical,
    _In_ ULONG RootEntryCount,
    _In_ ULONGLONG Va,
    _In_ ULONGLONG SizeInBytes,
    _In_ SOFTGPU_GPUVA_ACCESS Access);

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

#if (REACTOS_WDDM_TARGET_LEVEL >= 2100)
NTSTATUS
APIENTRY
SoftGpuDdiValidateUpdateAllocationProperty(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_VALIDATEUPDATEALLOCPROPERTY
        *ValidateUpdateAllocationProperty);
#endif


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

#if (REACTOS_WDDM_TARGET_LEVEL >= 2300)
NTSTATUS
APIENTRY
SoftGpuDdiSetTimingsFromVidPn(
    _In_ PVOID MiniportDeviceContext,
    IN_OUT_PDXGKARG_SETTIMINGSFROMVIDPN SetTimings);
#endif

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


/* =========================================================================
 * Function prototypes — wddm2.c (WDDM 2.0 DDIs)
 *
 * These are the WDDM 2.0 additions to DRIVER_INITIALIZATION_DATA that exist at
 * DXGKDDI_INTERFACE_VERSION_WDDM2_0 (0x5023).  They use the exact PFN typedefs
 * from d3dkmddi.h (via dispmprt.h); the IN_CONST_* / INOUT_* SAL aliases are
 * expanded inline so the signatures bind cleanly to the table fields.
 * ========================================================================= */

NTSTATUS
APIENTRY
SoftGpuDdiCreateProcess(
    _In_    CONST HANDLE          hAdapter,
    _Inout_ DXGKARG_CREATEPROCESS *pCreateProcess);

NTSTATUS
APIENTRY
SoftGpuDdiDestroyProcess(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST HANDLE hKmdProcess);

SIZE_T
APIENTRY
SoftGpuDdiGetRootPageTableSize(
    _In_    CONST HANDLE                  hAdapter,
    _Inout_ DXGKARG_GETROOTPAGETABLESIZE *pArgs);

VOID
APIENTRY
SoftGpuDdiSetRootPageTable(
    _In_ CONST HANDLE                      hAdapter,
    _In_ CONST DXGKARG_SETROOTPAGETABLE   *pSetPageTable);

NTSTATUS
APIENTRY
SoftGpuDdiMapCpuHostAperture(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_MAPCPUHOSTAPERTURE  *pArgs);

NTSTATUS
APIENTRY
SoftGpuDdiUnmapCpuHostAperture(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_UNMAPCPUHOSTAPERTURE  *pArgs);

NTSTATUS
APIENTRY
SoftGpuDdiSubmitCommandVirtual(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMANDVIRTUAL  *pSubmitCommand);

/* EOF */
