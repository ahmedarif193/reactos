/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Master internal header for dxgkrnl.sys — defines all private
 *              data structures, global state, and function prototypes shared
 *              across all translation units in the dxgkrnl module.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Design notes
 * ============
 * dxgkrnl follows the same two-layer model used by videoprt.sys and
 * storport.sys:
 *
 *   DXGKRNL_MINIPORT_CONTEXT  — one per miniport DRIVER_OBJECT, allocated
 *       as a DriverObjectExtension via IoAllocateDriverObjectExtension.
 *       Stores the miniport's DDI callback table and registry path.
 *
 *   DXGKRNL_ADAPTER           — one per physical GPU (FDO DeviceExtension).
 *       Stores PnP/power state, miniport opaque context, interrupt/DPC
 *       objects, resource lists, child/segment/node counts, and the
 *       per-adapter device list.
 *
 * User-mode GPU state is tracked through two additional structures:
 *
 *   DXGKRNL_DEVICE            — one per D3DKMT logical device.
 *   DXGKRNL_CONTEXT           — one per D3DKMT execution context.
 *
 * Video memory state:
 *
 *   DXGKRNL_ALLOCATION        — one per VIDMM allocation.
 *
 * Per-process state:
 *
 *   DXGKRNL_PROCESS           — one per user-mode process that has opened
 *       a GPU adapter handle.
 */

#ifndef _DXGKRNL_PRIVATE_H_
#define _DXGKRNL_PRIVATE_H_

/* ---- Minimum OS version for dxgkrnl ------------------------------------ */
/*
 * dxgkrnl.sys targets the Windows 7 WDDM 1.1 stack.
 * Override both _WIN32_WINNT and NTDDI_VERSION before including any kernel
 * headers so that Win7-era declarations are visible.
 *
 * ReactOS still accepts Vista-class full WDDM miniports for compatibility,
 * but the kernel-side contract we advertise and build against is Win7.
 *
 * Functions such as
 * PsSetCreateProcessNotifyRoutineEx are declared.
 *
 * The ReactOS build system sets _WIN32_WINNT=0x0502 (Server 2003) globally,
 * but dxgkrnl is a WDDM driver that only loads on Vista+.  The
 * sdkddkver.h consistency check requires (NTDDI_VERSION >> 16) == _WIN32_WINNT,
 * so both values must be updated together.
 */
#undef  _WIN32_WINNT
#define _WIN32_WINNT  0x0601    /* Windows 7 */
#undef  NTDDI_VERSION
#define NTDDI_VERSION 0x06010000 /* NTDDI_WIN7 */

/* ---- Standard kernel headers ------------------------------------------- */
#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>

/* ---- WDDM DDI interface version selection ------------------------------ */
/*
 * Default to the Win7 WDDM 1.1 interface when the build system did not
 * supply an explicit version. Full-WDDM miniports newer than Win7 are
 * rejected at registration time; display-only fallback drivers are handled
 * separately.
 */
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WIN7
#endif

/* ---- Windows base type definitions ------------------------------------- */
/*
 * d3dukmdt.h (pulled in transitively by d3dkmddi.h) uses UINT before any
 * kernel header defines that type.  windef.h supplies it via minwindef.h.
 * This is safe in kernel mode — other ReactOS kernel drivers do the same.
 */
#include <windef.h>

/* ---- WDDM public interface headers ------------------------------------- */
/*
 * dispmprt.h is the primary WDDM display miniport header.  It includes
 * d3dkmddi.h (which includes d3dkmdt.h -> d3dukmdt.h) and provides all
 * DXGK_*, DXGKDDI_*, DXGKARG_* and DRIVER_INITIALIZATION_DATA types.
 *
 * d3dkmthk.h provides D3DKMT_* types for the kernel-mode thunk layer
 * (D3DKMT_CREATEDEVICE, D3DKMT_HANDLE, etc.).  It includes d3dkmdt.h
 * transitively but the include guard prevents redefinition.
 */
#include <dispmprt.h>
#include <d3dkmthk.h>
#include "d3dkmt.h"

/* ---- ReactOS WDDM private adapter-level interface ---------------------- */
#include <reactos/rddm/rxgkinterface.h>

/* MmSystemRangeStart is a kernel global (user/kernel VA split boundary). It is
 * declared in the XDK arch mm.h, which the WDM include subset used here does not
 * pull in; re-declare it (used in user-VA range ASSERTs in vidmm.c/d3dkmt.c). */
#ifndef MmSystemRangeStart
extern NTKERNELAPI PVOID MmSystemRangeStart;
#endif

/* adapter.c flushes the TLB/store buffer after (re)mapping framebuffer pages
 * using the x86 intrinsics __invlpg/_mm_mfence. Provide ARM64 equivalents:
 * a full system DMB for the fence, and a broadcast TLB + DSB/ISB for the
 * mapping invalidation (heavier than a single-page invalidate, but correct and
 * only used on the one-time framebuffer mapping path). */
#if defined(_M_ARM64) || defined(_ARM64_)
/* Macros (not functions) so they do not clash with the weak __invlpg/_mm_mfence
 * prototypes the compiler intrinsics header declares. */
#undef _mm_mfence
#undef __invlpg
#define _mm_mfence() __asm__ __volatile__("dmb sy" ::: "memory")
#define __invlpg(Va) \
    do { (void)(Va); __asm__ __volatile__("dsb sy\n\ttlbi vmalle1is\n\tdsb sy\n\tisb" ::: "memory"); } while (0)
#endif

/* ---- Debug helpers ----------------------------------------------------- */
/*
 * Use the full path <reactos/debug.h> to avoid accidentally finding the
 * local dxgkrnl "debug.h" via the angle-bracket search path.
 * The local "debug.h" must come after <reactos/debug.h> because it uses
 * DPRINT / DPRINT1 macros defined by the system header.
 */
#define NDEBUG
#include <reactos/debug.h>
#include "debug.h"

/* Pool tag for display/vidpn-related allocations */
#define TAG_DXGK_DISPLAY 'DxgD'

/*
 * Helper macro to access miniport callbacks from either the full WDDM
 * layout (.s) or the DOD layout (.dod) depending on UseDodLayout.
 */
#define DXGK_CB(Adapter, Field) \
    ((Adapter)->MiniportContext->UseDodLayout \
        ? (Adapter)->MiniportContext->InitData.dod.Field \
        : (Adapter)->MiniportContext->InitData.s.Field)

/*
 * DXGK_CB_FULL — access a callback that only exists in the full WDDM
 * DRIVER_INITIALIZATION_DATA (.s), not in KMDDOD_INITIALIZATION_DATA (.dod).
 * Returns NULL for DOD adapters.  Always yields the full-WDDM typed pointer.
 */
#define DXGK_CB_FULL(Adapter, Field) \
    ((Adapter)->MiniportContext->UseDodLayout \
        ? (typeof((Adapter)->MiniportContext->InitData.s.Field))NULL \
        : (Adapter)->MiniportContext->InitData.s.Field)

typedef struct _DXGKVMM_ALLOCATION DXGKVMM_ALLOCATION, *PDXGKVMM_ALLOCATION;
typedef struct _DXGKVMM_RESOURCE DXGKVMM_RESOURCE, *PDXGKVMM_RESOURCE;

/* ========================================================================
 * Pool tags
 * ====================================================================== */
#define TAG_DXGK_MINIPORT   'MxgD'   /* DXGM - per-miniport context      */
#define TAG_DXGK_ADAPTER    'AxgD'   /* DXGA - per-adapter context        */
#define TAG_DXGK_DEVICE     'VxgD'   /* DXGV - per-device context         */
#define TAG_DXGK_CONTEXT    'CxgD'   /* DXGC - per-context                */
#define TAG_DXGK_ALLOC      'LxgD'   /* DXGL - per-allocation             */
#define TAG_DXGK_PROCESS    'PxgD'   /* DXGP - per-process state          */
#define TAG_DXGK_SYNC       'YxgD'   /* DXGY - sync objects               */
#define TAG_DXGK_REGISTRY   'RxgD'   /* DXGR - registry path buffer       */
#define TAG_DXGK_RESOURCES  'SxgD'   /* DXGS - segment/child descriptors  */
#define TAG_DXGK_SUBMITDMA  'QxgD'   /* DXGQ - tracked submit DMA buffers */

/* ========================================================================
 * Forward declarations
 * ====================================================================== */
typedef struct _DXGKRNL_MINIPORT_CONTEXT  DXGKRNL_MINIPORT_CONTEXT;
typedef struct _DXGKRNL_MINIPORT_CONTEXT *PDXGKRNL_MINIPORT_CONTEXT;

typedef struct _DXGKRNL_ADAPTER           DXGKRNL_ADAPTER;
typedef struct _DXGKRNL_ADAPTER          *PDXGKRNL_ADAPTER;

typedef struct _DXGKRNL_DEVICE            DXGKRNL_DEVICE;
typedef struct _DXGKRNL_DEVICE           *PDXGKRNL_DEVICE;

typedef struct _DXGKRNL_CONTEXT           DXGKRNL_CONTEXT;
typedef struct _DXGKRNL_CONTEXT          *PDXGKRNL_CONTEXT;

typedef struct _DXGKRNL_ALLOCATION        DXGKRNL_ALLOCATION;
typedef struct _DXGKRNL_ALLOCATION       *PDXGKRNL_ALLOCATION;

typedef struct _DXGKRNL_PROCESS           DXGKRNL_PROCESS;
typedef struct _DXGKRNL_PROCESS          *PDXGKRNL_PROCESS;

typedef struct _DXGKRNL_SUBMIT_DMA_BUFFER DXGKRNL_SUBMIT_DMA_BUFFER;
typedef struct _DXGKRNL_SUBMIT_DMA_BUFFER *PDXGKRNL_SUBMIT_DMA_BUFFER;

/* ========================================================================
 * DXGKARGCB_* — Callback argument structures (Vista WDK layout)
 *
 * These structures are used by the DxgkCb* callbacks that dxgkrnl exports
 * to the miniport.  They are not yet present in the ReactOS SDK so we
 * define them inline here.  All field offsets must match the Vista WDK.
 * ====================================================================== */

/*
 * DXGKARGCB_ALLOCATECONTIGUOUSMEMORY
 *
 * Argument structure for DxgkCbAllocateContiguousMemory.
 * Layout matches the Vista WDK / dispmprt.h definition.
 */
typedef struct _DXGKARGCB_ALLOCATECONTIGUOUSMEMORY
{
    SIZE_T              NumberOfBytes;              /* in:  bytes to allocate          */
    PHYSICAL_ADDRESS    LowestAcceptableAddress;    /* in:  min physical address       */
    PHYSICAL_ADDRESS    HighestAcceptableAddress;   /* in:  max physical address       */
    PHYSICAL_ADDRESS    BoundaryAddressMultiple;    /* in:  alignment boundary         */
    MEMORY_CACHING_TYPE CacheType;                  /* in:  cache type                 */
    PVOID               pContiguousMemoryAddress;   /* out: kernel VA of allocation    */
    PMDL                pMemoryDescriptorList;       /* out: MDL for the allocation     */
} DXGKARGCB_ALLOCATECONTIGUOUSMEMORY, *PDXGKARGCB_ALLOCATECONTIGUOUSMEMORY;

/*
 * DXGKARGCB_FREECONTIGUOUSMEMORY
 *
 * Argument structure for DxgkCbFreeContiguousMemory.
 */
typedef struct _DXGKARGCB_FREECONTIGUOUSMEMORY
{
    PVOID   pContiguousMemoryAddress;   /* in: kernel VA returned by AllocContiguous */
    PMDL    pMemoryDescriptorList;      /* in: MDL returned by AllocContiguous       */
} DXGKARGCB_FREECONTIGUOUSMEMORY, *PDXGKARGCB_FREECONTIGUOUSMEMORY;

/*
 * DXGKARGCB_MAPPHYSICALMEMORY
 *
 * Argument structure for DxgkCbMapPhysicalMemory.
 */
typedef struct _DXGKARGCB_MAPPHYSICALMEMORY
{
    PHYSICAL_ADDRESS    PhysicalAddress; /* in:  physical address to map      */
    SIZE_T              NumberOfBytes;   /* in:  byte count                   */
    MEMORY_CACHING_TYPE CacheType;       /* in:  cache attribute              */
    PVOID               pVirtualAddress; /* out: kernel VA                    */
} DXGKARGCB_MAPPHYSICALMEMORY, *PDXGKARGCB_MAPPHYSICALMEMORY;

/*
 * DXGKARGCB_UNMAP_PHYSICAL_MEMORY
 *
 * Argument structure for DxgkCbUnmapPhysicalMemory.
 */
typedef struct _DXGKARGCB_UNMAP_PHYSICAL_MEMORY
{
    PVOID   pVirtualAddress;    /* in: kernel VA returned by MapPhysicalMemory */
    SIZE_T  NumberOfBytes;      /* in: byte count                              */
} DXGKARGCB_UNMAP_PHYSICAL_MEMORY, *PDXGKARGCB_UNMAP_PHYSICAL_MEMORY;

/*
 * PDXGKCB_* callback typedefs and DXGK_INTERFACE are defined in dispmprt.h
 * (included above via dxgkrnl_private.h → dispmprt.h).  Do not redefine them
 * here to avoid duplicate-type conflicts.
 */

/*
 * DISPLAY_ADAPTER_HW_ID — UID passed to DxgkDdiSetPowerState to
 * target the adapter as a whole rather than a child display.
 */
#ifndef DISPLAY_ADAPTER_HW_ID
#define DISPLAY_ADAPTER_HW_ID  0xFFFEFFFFUL
#endif

/*
 * Minimum byte offset of the last required field.  We use this when
 * validating a miniport-supplied DriverInitDataSize: if the size is at
 * least this large the miniport has declared all WDDM 1.0 callbacks.
 */
#define DXGKRNL_DRIVER_INIT_DATA_MIN_SIZE \
    (FIELD_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetDisplayPrivateDriverFormat) + \
     sizeof(PDXGKDDI_SET_DISPLAY_PRIVATE_DRIVER_FORMAT))

/* ========================================================================
 * Adapter lifecycle states
 * ====================================================================== */
typedef enum _DXGKRNL_ADAPTER_STATE
{
    DxgkAdapterStateUninitialized  = 0,
    DxgkAdapterStateStopped,      /* AddDevice succeeded; Start not yet called */
    DxgkAdapterStateStarted,      /* DxgkDdiStartDevice succeeded             */
    DxgkAdapterStateSurpriseRemoved, /* IRP_MN_SURPRISE_REMOVAL received       */
    DxgkAdapterStateRemoved,      /* IRP_MN_REMOVE_DEVICE received             */
} DXGKRNL_ADAPTER_STATE;

/* ========================================================================
 * DXGKRNL_MINIPORT_CONTEXT
 *
 * Allocated as a DriverObjectExtension for the miniport's DRIVER_OBJECT
 * (see IoAllocateDriverObjectExtension).  Its lifetime is bounded by the
 * miniport's driver lifetime.
 * ====================================================================== */
struct _DXGKRNL_MINIPORT_CONTEXT
{
    /*
     * Copy of the miniport's callback table captured at DxgkInitialize
     * time.  We keep a full copy rather than a pointer because the
     * caller may use a stack-allocated struct.
     */
    /* Use a large buffer so WDDM 2.0+ miniports' full callback tables fit
     * even when dxgkrnl is compiled with a Vista-era struct definition. */
    union {
        DRIVER_INITIALIZATION_DATA      s;      /* Full WDDM driver layout */
        KMDDOD_INITIALIZATION_DATA      dod;    /* DOD driver layout */
        UCHAR                           Raw[1024];
    } InitData;

    /* Byte count actually provided by the miniport (for version tracking). */
    ULONG                       InitDataSize;

    /* TRUE if this miniport is a Display-Only Driver (DOD). */
    BOOLEAN                     IsDisplayOnlyDriver;

    /* TRUE if InitData was filled as KMDDOD_INITIALIZATION_DATA layout
     * (via DxgkInitializeDisplayOnlyDriver).  FALSE means the full
     * DRIVER_INITIALIZATION_DATA layout is used (via DxgkInitialize). */
    BOOLEAN                     UseDodLayout;

    /*
     * Copy of the miniport's RegistryPath (NUL-terminated buffer
     * allocated from NonPagedPool with TAG_DXGK_REGISTRY).
     */
    UNICODE_STRING              RegistryPath;

    /*
     * List of DXGKRNL_ADAPTER instances created by DxgkpAddDevice
     * for this miniport.  Protected by AdapterListLock at DIRQL;
     * alternatively acquired as a FAST_MUTEX at PASSIVE_LEVEL for
     * operations that do not run under interrupt context.
     */
    KSPIN_LOCK                  AdapterListLock;
    LIST_ENTRY                  AdapterListHead;
    ULONG                       AdapterCount;
};

typedef struct _DXGKRNL_SUBMIT_DMA_BUFFER
{
    LIST_ENTRY                  ListEntry;
    ULONG                       SubmissionFenceId;
    ULONG64                     RefreshPresentId;
    PVOID                       Buffer;
    ULONG                       Tag;
    PDXGKRNL_DEVICE             Device;
    HANDLE                      SourceAllocationHandle;
    HANDLE                      RefreshAllocationHandle;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID RefreshVidPnSourceId;
    RECT                        RefreshDstRect;
    BOOLEAN                     RefreshSharedPrimaryOnRetire;
    PHANDLE                     OpenHandleList;
    UINT                        OpenHandleCount;
} DXGKRNL_SUBMIT_DMA_BUFFER, *PDXGKRNL_SUBMIT_DMA_BUFFER;

/* ========================================================================
 * DXGKRNL_ADAPTER
 *
 * Stored in the FDO's DeviceExtension.  Lifetime: AddDevice → RemoveDevice.
 * ====================================================================== */
struct _DXGKRNL_ADAPTER
{
    /* Back-pointer to the per-miniport driver context. */
    PDXGKRNL_MINIPORT_CONTEXT   MiniportContext;

    /*
     * PnP device object trio.
     *   FunctionalDeviceObject  — FDO created by dxgkrnl in AddDevice.
     *   PhysicalDeviceObject    — PDO supplied by the bus driver.
     *   LowerDeviceObject       — result of IoAttachDeviceToDeviceStack.
     */
    PDEVICE_OBJECT              FunctionalDeviceObject;
    PDEVICE_OBJECT              PhysicalDeviceObject;
    PDEVICE_OBJECT              LowerDeviceObject;

    /*
     * Opaque per-device context returned by DxgkDdiAddDevice and passed
     * back as the first argument to every subsequent miniport DDI call.
     */
    PVOID                       MiniportDeviceContext;

    /* Current adapter lifecycle state. */
    DXGKRNL_ADAPTER_STATE       State;

    /*
     * Adapter LUID assigned during DxgkAdapterStart.
     * Used by D3DKMTEnumAdapters / D3DKMTOpenAdapterFromLuid to identify
     * adapters from user mode.
     */
    LUID                        AdapterLuid;

    /*
     * Opaque D3DKMT adapter handle returned to user mode.
     * Stored explicitly so validation compares handle identity instead of
     * reconstructing a 64-bit pointer from a 32-bit D3DKMT_HANDLE.
     */
    D3DKMT_HANDLE               Handle;

    /*
     * Topology reported by DxgkDdiStartDevice:
     *   NumberOfVideoPresentSources — display output count.
     *   NumberOfChildren            — child device count (monitors, etc.).
     */
    ULONG                       NumberOfVideoPresentSources;
    ULONG                       NumberOfChildren;

    /*
     * Child descriptor array, Pool-allocated with TAG_DXGK_RESOURCES.
     * Filled in during DxgkDdiQueryChildRelations; NULL until then.
     * Count matches NumberOfChildren.
     */
    PDXGK_CHILD_DESCRIPTOR      ChildDescriptors;

    /*
     * Runtime segment array (PDXGKRNL_SEGMENT), Pool-allocated with
     * TAG_VIDMM_SEGMENT by DxgkVidMmInitializeAdapter.  Typed PVOID to
     * avoid a circular header dependency on vidmm.h; cast to
     * PDXGKRNL_SEGMENT by vidmm.c callers.  NULL until adapter start.
     * Count is stored in SegmentCount.
     */
    PVOID                       Segments;           /* PDXGKRNL_SEGMENT */
    ULONG                       SegmentCount;

    /*
     * GPU engine / node count.
     * Filled in from DxgkDdiQueryAdapterInfo(DXGKQAITYPE_NUMPOWERCOMPONENTS)
     * or equivalent; governs scheduling policy.
     */
    ULONG                       NodeCount;

    /*
     * Interrupt object registered by dxgkrnl on behalf of the miniport
     * (IoConnectInterrupt / IoConnectInterruptEx).  NULL if the miniport
     * does not use line-based interrupts.
     */
    PKINTERRUPT                 InterruptObject;
    PIO_INTERRUPT_MESSAGE_INFO  InterruptMessageTable;
    BOOLEAN                     InterruptMessageBased;
    ULONG                       InterruptMessageCount;

    /* Saved interrupt resource info for deferred connection. */
    ULONG                       InterruptVector;
    KIRQL                       InterruptLevel;
    KAFFINITY                   InterruptAffinity;
    BOOLEAN                     InterruptShared;
    KINTERRUPT_MODE             InterruptMode;

    /*
     * SpinLock protecting the ISR ↔ DPC notification path.
     * Acquired at DIRQL inside the ISR; must be acquired at DIRQL
     * in any code that touches interrupt-sensitive fields.
     */
    KSPIN_LOCK                  InterruptLock;

    /*
     * DPC object queued by the ISR to defer miniport DPC work.
     * Calls MiniportContext->InitData.DxgkDdiDpcRoutine.
     */
    KDPC                        DpcObject;

    /*
     * Bounded interrupt-path tracing state used to debug init-time stalls.
     * These counters are reset at each DxgkAdapterStart and sampled from
     * the ISR / QueueDpc / DPC paths to avoid unbounded serial log spam.
     */
    volatile LONG               InterruptCount;
    volatile LONG               QueueDpcCount;
    volatile LONG               DpcCount;
    ULONGLONG                   InterruptTraceEpoch100ns;

    /*
     * Synchronisation event used by DxgkCbSynchronizeExecution to
     * coordinate between PASSIVE_LEVEL callers and the interrupt ISR.
     */
    KEVENT                      SyncEvent;

    /*
     * Mutex serialising all PASSIVE_LEVEL adapter state mutations
     * (device-list modifications, resource assignments, etc.).
     */
    FAST_MUTEX                  AdapterMutex;

    /*
     * Translated PCI resource lists captured at IRP_MN_START_DEVICE time.
     * Both pointers are NULL before Start or after Remove.
     */
    PCM_RESOURCE_LIST           AllocatedResources;     /* raw (bus-relative)  */
    PCM_RESOURCE_LIST           TranslatedResources;    /* translated (system) */

    /*
     * Cached PCI bus/slot number for this adapter.
     * Queried once during DxgkAdapterStart from IoGetDeviceProperty and
     * reused by DxgkCbReadDeviceSpace/DxgkCbWriteDeviceSpace to avoid
     * sending PnP IRPs (which can cause spinlock re-entrancy) on every
     * PCI config space access.
     */
    ULONG                       PciBusNumber;
    PCI_SLOT_NUMBER             PciSlotNumber;
    BOOLEAN                     PciBusSlotCached;

    /*
     * Power state tracking.
     */
    DEVICE_POWER_STATE          DevicePowerState;
    SYSTEM_POWER_STATE          SystemPowerState;

    /*
     * Child PDO linked list.  One entry per child device returned by
     * DxgkDdiQueryChildRelations.  Entries are DXGK_CHILD_PDO_EXTENSION
     * nodes linked via their ListEntry field.
     *
     * Protected by ChildListLock.  Must be acquired at PASSIVE_LEVEL only
     * (the lock is used with KeAcquireInStackQueuedSpinLock, which means
     * callers must not be at DISPATCH_LEVEL unless they hold no other
     * spinlocks that conflict with the IRQL ordering).
     */
    KSPIN_LOCK                  ChildListLock;
    LIST_ENTRY                  ChildListHead;
    ULONG                       ChildPdoCount;

    /*
     * List of DXGKRNL_DEVICE instances created against this adapter.
     * Protected by AdapterMutex at PASSIVE_LEVEL.
     */
    LIST_ENTRY                  DeviceListHead;

    /*
     * VidPN (Video Present Network) handle for this adapter.
     * Created at DxgkAdapterStart time; destroyed at DxgkAdapterStop.
     * Typed PVOID to avoid pulling vidpn.h into this header; cast to
     * PDXGKP_VIDPN by vidpn.c callers.  NULL until adapter start.
     */
    PVOID                       VidPn;              /* PDXGKP_VIDPN */

    /*
     * Shared primary resource exposed to win32k/cdd for full WDDM
     * adapters. Created lazily from the miniport's standard
     * shared-primary allocation data and opened through the D3DKMT
     * GetSharedPrimaryHandle/OpenResource path.
     */
    D3DKMT_HANDLE               SharedPrimaryResourceHandle;
    D3DKMT_HANDLE               SharedPrimaryGlobalShareHandle;
    HANDLE                      SharedPrimaryAllocationHandle;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SharedPrimaryVidPnSourceId;
    ULONG                       SharedPrimaryWidth;
    ULONG                       SharedPrimaryHeight;
    D3DDDIFORMAT                SharedPrimaryFormat;
    BOOLEAN                     SharedPrimaryIsGopBacked;
    D3DKMT_HANDLE               SharedShadowResourceHandle;
    D3DKMT_HANDLE               SharedShadowGlobalShareHandle;
    HANDLE                      SharedShadowAllocationHandle;
    ULONG                       SharedShadowWidth;
    ULONG                       SharedShadowHeight;
    ULONG                       SharedShadowPitch;
    D3DDDIFORMAT                SharedShadowFormat;

    /*
     * Shadow framebuffer tracking for DOD (Display Only Driver) present path.
     * ShadowFb is the kernel VA of the NonPagedPool shadow framebuffer that
     * framebuf.dll renders into. ShadowFbPitch is the byte stride and
     * ShadowFbSize is the total byte count.
     * Set during IOCTL_VIDEO_MAP_VIDEO_MEMORY; cleared at adapter stop.
     */
    PVOID                       ShadowFb;
    ULONG                       ShadowFbPitch;
    ULONG                       ShadowFbSize;

    /*
     * Display mode committed through CommitVidPn.  Set when the mode is
     * successfully committed to the miniport.
     */
    ULONG                       CommittedWidth;
    ULONG                       CommittedHeight;
    BOOLEAN                     VidPnCommitted;
    BOOLEAN                     SystemDisplayEnabled;
    D3DDDIFORMAT                SystemDisplayColorFormat;

    /* POST display info from DxgkCbAcquirePostDisplayOwnership. */
    ULONG                       PostDisplayWidth;
    ULONG                       PostDisplayHeight;
    PHYSICAL_ADDRESS            PostDisplayPhysicalAddress;
    ULONG                       PostDisplayPitch;
    PVOID                       PostDisplayVirtualAddress;
    SIZE_T                      PostDisplayMappingSize;

    /*
     * Periodic present timer.  Fires a DPC that calls
     * DxgkDdiPresentDisplayOnly to push the shadow framebuffer to the GPU.
     */
    KTIMER                      PresentTimer;
    KDPC                        PresentDpc;
    BOOLEAN                     PresentTimerActive;

    /*
     * Set by DWM via IOCTL while a composition BitBlt is in progress.
     * The present worker skips/retries when this is non-zero to avoid
     * copying a partially-drawn frame from the shadow framebuffer.
     */
    volatile LONG               DwmCompositionInProgress;

    /*
     * Serializes shadow-fb dirty-rect state shared by the display-control
     * IOCTL path, present timer DPC, and present work item.
     */
    KSPIN_LOCK                  PresentLock;

    /*
     * Tracks DMA buffers that remain owned by the miniport until it signals
     * DXGK_INTERRUPT_DMA_COMPLETED for their submission fence.
     */
    KSPIN_LOCK                  SubmitDmaLock;
    LIST_ENTRY                  SubmitDmaListHead;
    LIST_ENTRY                  SubmitDmaRetireListHead;
    WORK_QUEUE_ITEM             SubmitDmaRetireWorkItem;
    volatile LONG               SubmitDmaRetireWorkQueued;
    volatile LONG               NextSubmissionFenceId;
    volatile ULONG              LastCompletedSubmissionFenceId;

    /*
     * Per-VidPnSource present queues.
     *
     * PresentQueues is a pool-allocated array of DXGKRNL_PRESENT_QUEUE
     * structures (one per VidPn source).  Typed PVOID to avoid pulling
     * present.h into this header; cast to PDXGKRNL_PRESENT_QUEUE by
     * present.c callers.  NULL until DxgkPresentInit.
     * PresentQueueCount stores the number of entries in the array.
     */
    PVOID                       PresentQueues;      /* PDXGKRNL_PRESENT_QUEUE */
    ULONG                       PresentQueueCount;

    /*
     * Video Scheduler (VidSch) context for this adapter.
     * Typed PVOID to avoid pulling vidsch.h into this header; cast to
     * PVIDSCH_CONTEXT by vidsch.c callers.  NULL for DOD adapters or
     * until VidSchInitialize is called.
     */
    PVOID                       VidSchContext;       /* PVIDSCH_CONTEXT */

    /*
     * GUID_DISPLAY_DEVICE_ARRIVAL device interface.
     * Registered in DxgkpAddDevice, enabled in DxgkAdapterStart,
     * disabled and freed in DxgkAdapterRemove.
     */
    UNICODE_STRING              DeviceInterfaceName;
    BOOLEAN                     DeviceInterfaceEnabled;

    /*
     * Linkage in the per-miniport AdapterListHead AND in the global
     * DxgkAdapterGlobalListHead.
     */
    LIST_ENTRY                  MiniportAdapterListEntry;
    LIST_ENTRY                  GlobalAdapterListEntry;
};

/* Convenience macro: cast a PDEVICE_OBJECT to its DXGKRNL_ADAPTER extension. */
#define DXGKRNL_ADAPTER_FROM_DEVOBJ(DevObj) \
    ((PDXGKRNL_ADAPTER)((DevObj)->DeviceExtension))

/* ========================================================================
 * DXGKRNL_DEVICE — Per-D3D logical device
 *
 * Created via D3DKMT_CREATEDEVICE, destroyed by D3DKMT_DESTROYDEVICE.
 * Multiple devices can exist simultaneously per adapter.
 * Allocated from NonPagedPool with TAG_DXGK_DEVICE.
 * ====================================================================== */
struct _DXGKRNL_DEVICE
{
    /* Back-pointer to the owning adapter. */
    PDXGKRNL_ADAPTER            Adapter;

    /*
     * Opaque D3DKMT_HANDLE returned to the caller.  dxgkrnl uses the
     * pointer to the structure cast to ULONG_PTR as the handle value so
     * that lookups are O(1) without a separate handle table.
     */
    D3DKMT_HANDLE               Handle;

    /* Creation flags (D3DKMT_CREATEDEVICEFLAGS). */
    D3DKMT_CREATEDEVICEFLAGS    Flags;

    /*
     * Miniport-side device handle returned from DxgkDdiCreateDevice.
     * Passed back as hDevice in per-device DDI calls.
     */
    HANDLE                      hMiniportDevice;

    /*
     * Linkage in Adapter->DeviceListHead.
     * Protected by Adapter->AdapterMutex.
     */
    LIST_ENTRY                  DeviceListEntry;

    /*
     * List of DXGKRNL_CONTEXT objects created against this device.
     * Protected by DeviceMutex.
     */
    LIST_ENTRY                  ContextListHead;

    /*
     * List of D3DKMT_SYNCHRONIZATIONOBJECT handles created against this
     * device.  Protected by DeviceMutex.
     */
    LIST_ENTRY                  SyncObjListHead;

    FAST_MUTEX                  DeviceMutex;
};

/* ========================================================================
 * DXGKRNL_CONTEXT — Per-D3D execution context (GPU command stream)
 *
 * Created via D3DKMT_CREATECONTEXT, destroyed by D3DKMT_DESTROYCONTEXT.
 * Allocated from NonPagedPool with TAG_DXGK_CONTEXT.
 * ====================================================================== */
struct _DXGKRNL_CONTEXT
{
    /* Back-pointer to the owning device. */
    PDXGKRNL_DEVICE             Device;

    /*
     * Opaque D3DKMT_HANDLE returned to the caller (pointer-as-handle).
     */
    D3DKMT_HANDLE               Handle;

    /*
     * GPU engine routing supplied by the caller at creation time.
     *   NodeOrdinal    — zero-based GPU node index.
     *   EngineAffinity — bitmask of engines within that node.
     */
    UINT                        NodeOrdinal;
    UINT                        EngineAffinity;
    INT                         SchedulingPriority;

    /*
     * Miniport-side context handle returned from DxgkDdiCreateContext.
     */
    HANDLE                      hMiniportContext;

    /*
     * Linkage in Device->ContextListHead.
     * Protected by Device->DeviceMutex.
     */
    LIST_ENTRY                  ContextListEntry;
};

/* ========================================================================
 * DXGKRNL_ALLOCATION — GPU memory allocation descriptor
 *
 * One instance per VIDMM allocation, from creation through eviction and
 * eventual destruction.
 * Allocated from NonPagedPool with TAG_DXGK_ALLOC.
 * ====================================================================== */
struct _DXGKRNL_ALLOCATION
{
    /* Allocation size in bytes as reported by the miniport. */
    SIZE_T                      Size;

    /* Required base alignment in bytes (must be a power of two). */
    ULONG                       Alignment;

    /* Miniport-supplied allocation attribute flags. */
    DXGK_ALLOCATIONINFOFLAGS    Flags;

    /*
     * Physical base address when the allocation is committed to a segment.
     * QuadPart == 0 when the allocation is not currently resident.
     */
    PHYSICAL_ADDRESS            PhysicalAddress;

    /*
     * Segment identifier (1-based index into Adapter->SegmentDescriptors).
     * 0 means the allocation is not currently resident.
     */
    UINT                        SegmentId;

    /*
     * Miniport-side allocation handle returned from DxgkDdiCreateAllocation.
     * Passed back in per-allocation DDI calls.
     */
    HANDLE                      hMiniportAllocation;

    /*
     * Linkage in DXGKRNL_PROCESS->AllocationListHead.
     * Protected by DXGKRNL_PROCESS->ProcessMutex.
     */
    LIST_ENTRY                  AllocationListEntry;
};

/* ========================================================================
 * DXGKRNL_GPUVA_RANGE — Tracks one GPU VA region in a process address space
 *
 * Each range represents a contiguous region of GPU virtual address space
 * that is either reserved (no backing allocation) or mapped to an
 * allocation.  Ranges are linked in ascending GpuVirtualAddress order.
 * ====================================================================== */
typedef enum _DXGKRNL_GPUVA_STATE
{
    GpuVaStateFree      = 0,    /* Not allocated (should not be in list) */
    GpuVaStateReserved  = 1,    /* Reserved, no backing allocation       */
    GpuVaStateMapped    = 2,    /* Mapped to an allocation               */
} DXGKRNL_GPUVA_STATE;

typedef struct _DXGKRNL_GPUVA_RANGE
{
    /* GPU virtual address of the start of this range. */
    D3DGPU_VIRTUAL_ADDRESS      GpuVirtualAddress;

    /* Size of this range in bytes. */
    ULONGLONG                   SizeInBytes;

    /* State: reserved or mapped. */
    DXGKRNL_GPUVA_STATE         State;

    /*
     * When State == GpuVaStateMapped: handle of the backing allocation.
     * When State == GpuVaStateReserved: NULL.
     */
    HANDLE                      hAllocation;

    /* Byte offset within the allocation where this mapping starts. */
    ULONGLONG                   AllocationOffset;

    /*
     * Linkage in DXGKRNL_PROCESS->GpuVaRangeList (ascending VA order).
     * Protected by DXGKRNL_PROCESS->GpuVaLock.
     */
    LIST_ENTRY                  RangeListEntry;

} DXGKRNL_GPUVA_RANGE, *PDXGKRNL_GPUVA_RANGE;

/* Pool tag for GPU VA range objects. */
#define TAG_DXGK_GPUVA     'GVxD'   /* DXVG - GPU VA range */

/* ========================================================================
 * DXGKRNL_PROCESS — Per-process GPU state
 *
 * One instance per user-mode process that has opened a GPU adapter handle.
 * Created on first D3DKMT call from the process; cleaned up when the
 * process exits or closes all GPU handles.
 * Allocated from NonPagedPool with TAG_DXGK_PROCESS.
 *
 * Win7-targeted builds do not enable per-process GPU virtual address space
 * management. Those fields only exist when the tree is explicitly retargeted
 * to WDDM 2.0+.
 * ====================================================================== */
struct _DXGKRNL_PROCESS
{
    /*
     * Kernel handle to the owning process, opened with
     * ObOpenObjectByPointer and kept open for the structure's lifetime.
     */
    HANDLE                      ProcessHandle;

    /*
     * List of DXGKRNL_DEVICE objects created by this process.
     * Protected by ProcessMutex.
     */
    LIST_ENTRY                  DeviceListHead;

    /*
     * List of DXGKRNL_ALLOCATION objects directly owned by this process.
     * Protected by ProcessMutex.
     */
    LIST_ENTRY                  AllocationListHead;

    FAST_MUTEX                  ProcessMutex;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    /* ---- WDDM 2.0: Per-process GPU VA space management --------------- */

    /*
     * Back-pointer to the adapter this process is bound to.
     * Set during DxgkDdiCreateProcess; used for DDI calls.
     */
    PDXGKRNL_ADAPTER            Adapter;

    /*
     * Miniport-side process handle returned by DxgkDdiCreateProcess.
     * Passed back to DxgkDdiDestroyProcess on cleanup.
     */
    HANDLE                      hMiniportProcess;

    /*
     * Root page table allocation handle (dxgkrnl-managed).
     * Created when the process GPU VA space is initialized.
     */
    HANDLE                      hRootPageTable;

    /*
     * Physical address of the root page table.
     * Programmed into each context via DxgkDdiSetRootPageTable.
     */
    D3DGPU_PHYSICAL_ADDRESS     RootPageTableAddress;

    /*
     * Number of entries in the root page table.
     * Determined by DxgkDdiGetRootPageTableSize.
     */
    UINT                        RootPageTableEntries;

    /*
     * GPU VA range list: sorted doubly-linked list of DXGKRNL_GPUVA_RANGE.
     * Protected by GpuVaLock.
     */
    LIST_ENTRY                  GpuVaRangeList;
    FAST_MUTEX                  GpuVaLock;

    /*
     * Total bytes of GPU VA space reserved or mapped by this process.
     * Used for per-process residency budget enforcement.
     */
    ULONGLONG                   GpuVaTotalReserved;
    ULONGLONG                   GpuVaTotalMapped;

    /*
     * WDDM 2.0 residency budget.
     * ResidencyBudget is the maximum bytes that should be resident.
     * ResidentBytes tracks the current resident set size.
     * ResidencyBudgetEvent is signalled when ResidentBytes exceeds the budget.
     */
    ULONGLONG                   ResidencyBudget;
    ULONGLONG                   ResidentBytes;
    KEVENT                      ResidencyBudgetEvent;

    /*
     * MakeResident reference counts per allocation (implicit via alloc list).
     * A MakeResident/Evict call pair increments/decrements the reference count;
     * the allocation is only evicted when the count reaches zero.
     */

    /*
     * Linkage in global process list for enumeration during cleanup.
     */
    LIST_ENTRY                  GlobalProcessListEntry;
#else
    /*
     * Linkage in global process list for enumeration during cleanup.
     */
    LIST_ENTRY                  GlobalProcessListEntry;
#endif
};

/* ========================================================================
 * DXGKRNL_SYNC_OBJECT — GPU synchronisation object
 *
 * Represents a fence, semaphore, or mutex used to synchronise GPU/CPU work.
 * Created by D3DKMTCreateSynchronizationObject, destroyed by
 * D3DKMTDestroySynchronizationObject.
 * Allocated from NonPagedPool with TAG_DXGK_SYNC.
 * ====================================================================== */
typedef struct _DXGKRNL_SYNC_OBJECT
{
    D3DKMT_HANDLE               Handle;
    D3DKMT_HANDLE               hDevice;
    D3DDDI_SYNCHRONIZATIONOBJECTINFO Info;
    volatile LONG               RefCount;
    volatile LONG64             FenceValue;
    KEVENT                      CpuEvent;
    LIST_ENTRY                  SyncObjListEntry;
    LIST_ENTRY                  DeviceSyncObjListEntry;
} DXGKRNL_SYNC_OBJECT, *PDXGKRNL_SYNC_OBJECT;

/* ========================================================================
 * Global state (defined in dxgkrnl.c, declared extern here)
 * ====================================================================== */

/*
 * DxgkAdapterGlobalListHead / DxgkAdapterGlobalListLock
 *
 * System-wide list of all active DXGKRNL_ADAPTER instances across all
 * registered miniports.  Linked via GlobalAdapterListEntry.
 *
 * The spinlock is used at DIRQL in the ISR path; all PASSIVE_LEVEL
 * callers should use the per-adapter AdapterMutex instead.
 */
extern KSPIN_LOCK   DxgkAdapterGlobalListLock;
extern LIST_ENTRY   DxgkAdapterGlobalListHead;

/*
 * GDxgControlDeviceObject
 *
 * The \Device\DxgKrnl device object used to service D3DKMT IOCTLs from
 * user-mode (dxgi.dll, ICD shims, etc.).  Created in DriverEntry.
 */
extern PDEVICE_OBJECT   GDxgControlDeviceObject;

/*
 * GDxgmms1Interface
 *
 * Pointer to the dxgmms1 function table, set when dxgmms1 registers
 * itself through the private interface.  NULL until then.
 */
extern PVOID    GDxgmms1Interface;

/* ========================================================================
 * Function prototypes — dxgkrnl.c  (module entry point and exports)
 * ====================================================================== */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

/*
 * DxgkInitialize / DxgkInitializeEx
 *
 * Called from the miniport's DriverEntry.  They:
 *   1. Validate the callback table.
 *   2. Allocate and populate a DXGKRNL_MINIPORT_CONTEXT.
 *   3. Hook the miniport DriverObject (AddDevice, PnP, Power, Unload).
 */
NTSTATUS
APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData);

NTSTATUS
APIENTRY
DxgkInitializeEx(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ ULONG                       DriverInitDataSize,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData);

/*
 * DxgkCb* callbacks — prototypes match the PDXGKCB_* typedefs in dispmprt.h.
 * The memory-related callbacks (AllocateContiguous, FreeContiguous,
 * MapPhysical, UnmapPhysical, AcquirePostDisplay) use PVOID for the
 * argument structure to remain type-compatible with the DXGK_INTERFACE
 * function-pointer table.  Implementations cast PVOID to the specific
 * PDXGKARGCB_* type internally.
 */

NTSTATUS
APIENTRY
DxgkCbNotifyInterrupt(
    _In_ HANDLE                                    DeviceHandle,
    _In_ CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA    *NotifyInterruptData);

VOID
APIENTRY
DxgkCbNotifyDpc(
    _In_ HANDLE DeviceHandle);

NTSTATUS
APIENTRY
DxgkCbAllocateContiguousMemory(
    _In_    HANDLE  DeviceHandle,
    _Inout_ PVOID   AllocContiguousMemory);

NTSTATUS
APIENTRY
DxgkCbFreeContiguousMemory(
    _In_ HANDLE  DeviceHandle,
    _In_ PVOID   FreeContiguousMemory);

NTSTATUS
APIENTRY
DxgkCbMapPhysicalMemory(
    _In_    HANDLE  DeviceHandle,
    _Inout_ PVOID   MapPhysicalMemory);

NTSTATUS
APIENTRY
DxgkCbUnmapPhysicalMemory(
    _In_ HANDLE  DeviceHandle,
    _In_ PVOID   UnmapPhysicalMemory);

NTSTATUS
APIENTRY
DxgkCbGetDeviceInformation(
    _In_  HANDLE              DeviceHandle,
    _Out_ PDXGK_DEVICE_INFO   DeviceInformation);

NTSTATUS
APIENTRY
DxgkCbIndicateChildStatus(
    _In_ HANDLE              DeviceHandle,
    _In_ PDXGK_CHILD_STATUS  ChildStatus);

NTSTATUS
APIENTRY
DxgkCbSynchronizeExecution(
    _In_  HANDLE                  DeviceHandle,
    _In_  PKSYNCHRONIZE_ROUTINE   SynchronizeRoutine,
    _In_  PVOID                   Context,
    _In_  ULONG                   MessageNumber,
    _Out_ PBOOLEAN                ReturnValue);

/* WDDM 1.0 memory-mapping callback (individual parameters, not struct). */
NTSTATUS
APIENTRY
DxgkCbMapMemory(
    _In_  HANDLE              DeviceHandle,
    _In_  PHYSICAL_ADDRESS    TranslatedAddress,
    _In_  ULONG               Length,
    _In_  BOOLEAN             InIoSpace,
    _In_  BOOLEAN             MapToUserMode,
    _In_  MEMORY_CACHING_TYPE CacheType,
    _Out_ PVOID              *VirtualAddress);

/* WDDM 1.0 unmap callback (simpler signature than WDDM 2.9 variant). */
NTSTATUS
APIENTRY
DxgkCbUnmapMemory(
    _In_ HANDLE DeviceHandle,
    _In_ PVOID  VirtualAddress);

/* Queue the adapter DPC from ISR context. */
BOOLEAN
APIENTRY
DxgkCbQueueDpc(
    _In_ HANDLE DeviceHandle);

/* Read from PCI configuration space or expansion ROM. */
NTSTATUS
APIENTRY
DxgkCbReadDeviceSpace(
    _In_  HANDLE  DeviceHandle,
    _In_  ULONG   DataType,
    _In_  PVOID   Buffer,
    _In_  ULONG   Offset,
    _In_  ULONG   Length,
    _Out_ PULONG  BytesRead);

/* Write to PCI configuration space. */
NTSTATUS
APIENTRY
DxgkCbWriteDeviceSpace(
    _In_  HANDLE  DeviceHandle,
    _In_  ULONG   DataType,
    _In_  PVOID   Buffer,
    _In_  ULONG   Offset,
    _In_  ULONG   Length,
    _Out_ PULONG  BytesWritten);

NTSTATUS
APIENTRY
DxgkCbAcquirePostDisplayOwnership(
    _In_  HANDLE  DeviceHandle,
    _Out_ PVOID   DisplayInformation);

/* ========================================================================
 * Function prototypes — adapter.c
 * ====================================================================== */

/* Installed into miniport DriverObject->DriverExtension->AddDevice */
DRIVER_ADD_DEVICE DxgkpAddDevice;

/* Installed into miniport DriverObject->MajorFunction[IRP_MJ_PNP] */
DRIVER_DISPATCH DxgkpMiniportPnpDispatch;

/* Installed into miniport DriverObject->MajorFunction[IRP_MJ_POWER] */
DRIVER_DISPATCH DxgkpMiniportPowerDispatch;

/* Installed into miniport DriverObject->DriverUnload */
DRIVER_UNLOAD DxgkpDriverUnload;

/* Per-adapter lifecycle helpers */
NTSTATUS
DxgkAdapterStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PCM_RESOURCE_LIST AllocatedResources,
    _In_ PCM_RESOURCE_LIST TranslatedResources);

NTSTATUS
DxgkAdapterStop(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkAdapterRemove(
    _In_ PDXGKRNL_ADAPTER Adapter);

/* ========================================================================
 * Function prototypes — pnp.c
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

NTSTATUS
NTAPI
DxgkDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

/* ========================================================================
 * Function prototypes — vidmm.c  (video memory manager)
 * ====================================================================== */

/*
 * DxgkVidMmInitializeAdapter / DxgkVidMmTeardownAdapter
 *
 * Adapter-level segment init/teardown.  Called from DxgkAdapterStart /
 * DxgkAdapterStop in adapter.c.  vidmm.h has the detailed prototypes for
 * internal vidmm.c helpers; these two are the only entry points visible to
 * adapter.c.
 */
NTSTATUS
DxgkVidMmInitializeAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkVidMmTeardownAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkVidMmCreateAllocation — create a GPU allocation (internal).
 *
 * Signature matches the implementation in vidmm.c:
 *   Adapter   — owning adapter
 *   DeviceCtx — optional DXGKRNL_DEVICE pointer (may be NULL)
 *   AllocInfo — filled DXGK_ALLOCATIONINFO (from miniport)
 *   OutHandle — receives opaque HANDLE for the allocation
 */
NTSTATUS
DxgkVidMmCreateAllocation(
    _In_      PDXGKRNL_ADAPTER       Adapter,
    _In_opt_  PDXGKRNL_DEVICE        Device,
    _In_      DXGK_ALLOCATIONINFO   *AllocInfo,
    _In_opt_  CONST VOID            *CreatePrivateDriverData,
    _In_      UINT                   CreatePrivateDriverDataSize,
    _In_opt_  HANDLE                 ResourceHandle,
    _In_      DXGK_CREATEALLOCATIONFLAGS CreateFlags,
    _Out_     PHANDLE                OutHandle,
    _Out_opt_ PHANDLE                OutResourceHandle);

/*
 * DxgkVidMmDestroyAllocation — destroy a GPU allocation by handle.
 */
NTSTATUS
DxgkVidMmDestroyAllocation(
    _In_ PDXGKRNL_ADAPTER   Adapter,
    _In_ HANDLE             AllocationHandle);

PDXGKVMM_ALLOCATION
DxgkVidMmHandleToAllocation(
    _In_opt_ HANDLE Handle);

PDXGKVMM_RESOURCE
DxgkVidMmHandleToResource(
    _In_ D3DKMT_HANDLE Handle);

PDXGKVMM_RESOURCE
DxgkVidMmHandleToGlobalShare(
    _In_ D3DKMT_HANDLE Handle);

PDXGKVMM_RESOURCE
DxgkVidMmCreateResourceWrapper(
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_opt_ HANDLE          MiniportHandle,
    _In_ D3DKMT_HANDLE       GlobalShareHandle,
    _In_opt_ HANDLE          AllocationHandle,
    _In_reads_bytes_opt_(ResourcePrivateDriverDataSize)
            CONST VOID      *ResourcePrivateDriverData,
    _In_    UINT             ResourcePrivateDriverDataSize);

NTSTATUS
DxgkpVidMmDestroyResourceWrapper(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_RESOURCE Resource);

NTSTATUS
DxgkVidMmMapAllocationCpu(
    _In_  PDXGKVMM_ALLOCATION Allocation,
    _Out_ PVOID              *OutVa);

NTSTATUS
DxgkVidMmMapAllocationUser(
    _In_  PDXGKVMM_ALLOCATION Allocation,
    _Out_ PVOID              *OutVa);

VOID
DxgkVidMmUnmapAllocationCpu(
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmUnmapAllocationUser(
    _In_ PDXGKVMM_ALLOCATION Allocation);

NTSTATUS
DxgkVidMmEnsureAllocationApertureMapped(
    _In_ PDXGKVMM_ALLOCATION Allocation);

LARGE_INTEGER
DxgkVidMmGetAllocationPrimaryAddress(
    _In_ PDXGKVMM_ALLOCATION Allocation);

NTSTATUS
DxgkCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION *pCreateAllocation);

NTSTATUS
DxgkDestroyAllocation(
    _In_ CONST D3DKMT_DESTROYALLOCATION *pDestroyAllocation);

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
/* ========================================================================
 * Function prototypes — gpuva.c  (GPU virtual address management, WDDM 2.0)
 * ====================================================================== */

/*
 * GPU VA process lifecycle — called from adapter/context code.
 */
NTSTATUS
DxgkGpuVaCreateProcess(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _In_  PDXGKRNL_PROCESS  Process);

VOID
DxgkGpuVaDestroyProcess(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process);

/*
 * GPU VA range management — core operations.
 */
NTSTATUS
DxgkGpuVaReserve(
    _In_  PDXGKRNL_PROCESS         Process,
    _In_  D3DGPU_VIRTUAL_ADDRESS   BaseAddress,
    _In_  ULONGLONG                SizeInBytes,
    _Out_ D3DGPU_VIRTUAL_ADDRESS  *OutAddress);

NTSTATUS
DxgkGpuVaMap(
    _In_    PDXGKRNL_PROCESS       Process,
    _In_    HANDLE                 hAllocation,
    _In_    ULONGLONG              AllocationOffset,
    _In_    ULONGLONG              SizeInBytes,
    _In_    D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_    D3DGPU_VIRTUAL_ADDRESS MinAddress,
    _In_    D3DGPU_VIRTUAL_ADDRESS MaxAddress,
    _Out_   D3DGPU_VIRTUAL_ADDRESS *OutAddress);

NTSTATUS
DxgkGpuVaFree(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_ ULONGLONG              SizeInBytes);

/*
 * GPU VA residency management (WDDM 2.0).
 */
NTSTATUS
DxgkGpuVaMakeResident(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_    HANDLE            *AllocationList,
    _In_    ULONG              NumAllocations,
    _Out_   ULONGLONG         *OutBytesResidency);

NTSTATUS
DxgkGpuVaEvict(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_    HANDLE            *AllocationList,
    _In_    ULONG              NumAllocations,
    _Out_   ULONGLONG         *OutNumBytesToTrim);

/*
 * Root page table management.
 */
NTSTATUS
DxgkGpuVaSetRootPageTable(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process,
    _In_ PDXGKRNL_CONTEXT  Context);

SIZE_T
DxgkGpuVaGetRootPageTableSize(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ UINT              NumberOfPte,
    _In_ UINT              PhysicalAdapterIndex);

/*
 * CPU host aperture mapping.
 */
NTSTATUS
DxgkGpuVaMapCpuHostAperture(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ HANDLE            hAllocation,
    _In_ WORD              SegmentId,
    _In_ UINT64            NumberOfPages,
    _In_ UINT32           *pCpuHostAperturePages,
    _In_ UINT64           *pMemorySegmentPages);

NTSTATUS
DxgkGpuVaUnmapCpuHostAperture(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ UINT64            NumberOfPages,
    _In_ UINT32           *pCpuHostAperturePages,
    _In_ WORD              SegmentId);
#endif

/* ========================================================================
 * Function prototypes — context.c
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkCreateDevice(
    _Inout_ D3DKMT_CREATEDEVICE *pCreateDevice);

NTSTATUS
NTAPI
DxgkDestroyDevice(
    _In_ D3DKMT_DESTROYDEVICE *pDestroyDevice);

NTSTATUS
NTAPI
DxgkCreateContext(
    _Inout_ D3DKMT_CREATECONTEXT *pCreateContext);

NTSTATUS
NTAPI
DxgkDestroyContext(
    _In_ D3DKMT_DESTROYCONTEXT *pDestroyContext);

/* ========================================================================
 * Function prototypes — dma.c  (command buffer / DMA submission)
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkRender(
    _Inout_ D3DKMT_RENDER *pRender);

NTSTATUS
NTAPI
DxgkPresent(
    _Inout_ D3DKMT_PRESENT *pPresent);

/* ========================================================================
 * Function prototypes — d3dkmt.c  (standalone D3DKMT handlers)
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkLock(
    _Inout_ D3DKMT_LOCK *pLock);

NTSTATUS
NTAPI
DxgkUnlock(
    _In_ CONST D3DKMT_UNLOCK *pUnlock);

NTSTATUS
NTAPI
DxgkEscape(
    _In_ CONST D3DKMT_ESCAPE *pEscape);

/* ========================================================================
 * Function prototypes — sync.c  (GPU synchronisation objects)
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkCreateSynchronizationObject(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT *pCreateSyncObject);

NTSTATUS
NTAPI
DxgkDestroySynchronizationObject(
    _In_ D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *pDestroySyncObject);

NTSTATUS
NTAPI
DxgkSignalSynchronizationObject(
    _In_ D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *pSignalSyncObject);

NTSTATUS
NTAPI
DxgkWaitForSynchronizationObject(
    _In_ D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *pWaitSyncObject);

NTSTATUS
NTAPI
DxgkSyncObjectCpuSignal(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue);

NTSTATUS
NTAPI
DxgkSyncObjectCpuWait(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue,
    _In_ BOOLEAN NonBlocking);

VOID
NTAPI
DxgkCleanupDeviceSynchronizationObjects(
    _In_ PDXGKRNL_DEVICE Device);

/* ========================================================================
 * Function prototypes — vidpn.c  (VidPN topology management)
 * ====================================================================== */

/*
 * VidPN lifecycle — called from adapter.c at StartDevice / StopDevice time.
 */
NTSTATUS
DxgkVidPnCreateForAdapter(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _Out_ D3DKMDT_HVIDPN   *phVidPn);

NTSTATUS
DxgkVidPnClone(
    _In_  D3DKMDT_HVIDPN  hSourceVidPn,
    _Out_ D3DKMDT_HVIDPN *phClonedVidPn);

VOID
DxgkVidPnDestroy(
    _In_ D3DKMDT_HVIDPN hVidPn);

NTSTATUS
DxgkVidPnRebuildForHotPlug(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkCbQueryVidPnInterface — DXGK_INTERFACE callback at offset 0x90.
 */
NTSTATUS
APIENTRY
DxgkCbQueryVidPnInterface(
    _In_  D3DKMDT_HVIDPN                       hVidPn,
    _In_  DXGK_VIDPN_INTERFACE_VERSION         VidPnInterfaceVersion,
    _Out_ CONST DXGK_VIDPN_INTERFACE**         ppVidPnInterface);

/*
 * DxgkCbQueryMonitorInterface — DXGK_INTERFACE callback at offset 0x98.
 */
NTSTATUS
APIENTRY
DxgkCbQueryMonitorInterface(
    _In_  HANDLE                               hAdapter,
    _In_  UINT                                 MonitorInterfaceVersion,
    _Out_ PVOID*                               ppMonitorInterface);

/*
 * D3DKMT stubs.
 */
NTSTATUS
NTAPI
DxgkSetDisplayMode(
    _In_ D3DKMT_SETDISPLAYMODE *pSetDisplayMode);

NTSTATUS
NTAPI
DxgkGetSharedPrimaryHandle(
    _Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE *pGetSharedPrimaryHandle);

NTSTATUS
NTAPI
DxgkGetShadowSurface(
    _Inout_ DXGKMT_GETSHADOWSURFACE *pGetShadowSurface);

NTSTATUS
NTAPI
DxgkQueryResourceInfo(
    _Inout_ D3DKMT_QUERYRESOURCEINFO *pQueryResourceInfo);

NTSTATUS
NTAPI
DxgkOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pOpenResource);

VOID
DxgkDestroySharedPrimary(
    _In_ PDXGKRNL_ADAPTER Adapter);

NTSTATUS
NTAPI
DxgkGetDisplayModeList(
    _Inout_ D3DKMT_GETDISPLAYMODELIST *pGetDisplayModeList);

NTSTATUS
NTAPI
DxgkSetVidPnSourceOwner(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner);

NTSTATUS
NTAPI
DxgkCheckVidPnExclusiveOwnership(
    _In_ CONST D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *pCheckVidPnExclusiveOwnership);

/* ========================================================================
 * Function prototypes — display.c  (WDDM ↔ win32ss display bridge)
 * ====================================================================== */

/*
 * DxgkDisplayRegister
 *
 * Creates \Device\Video0 and populates DEVICEMAP\VIDEO registry entries
 * so that win32ss can discover the WDDM adapter.  Called from
 * DxgkAdapterStart after the miniport has been successfully started.
 */
NTSTATUS
DxgkDisplayRegister(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkpEnsurePostDisplayResolution
 *
 * Fill Adapter->PostDisplayWidth/Height from the firmware GOP if no miniport
 * acquired POST display ownership. Keeps registry DefaultSettings, the pinned
 * VidPn mode and the shadow framebuffer all anchored to the real GOP size.
 */
VOID
DxgkpEnsurePostDisplayResolution(
    _Inout_ PDXGKRNL_ADAPTER Adapter);

NTSTATUS
DxgkDisplayEstablishInitialMode(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkDisplayUnregister
 *
 * Tears down \Device\Video0 and cleans up.
 * Called from DxgkAdapterStop / DxgkAdapterRemove.
 */
VOID
DxgkDisplayUnregister(VOID);

/*
 * Display dispatch helpers — called from dxgkrnl dispatch routines
 * to route IRPs targeting \Device\Video0 to the display handler.
 * Return TRUE if the IRP was handled, FALSE if not.
 */
BOOLEAN
DxgkDisplayDispatchIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

BOOLEAN
DxgkDisplayDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

BOOLEAN
DxgkDisplayDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

BOOLEAN
DxgkDisplayDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

NTSTATUS
DxgkDisplayCommitVidPn(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkpStartPresentTimer(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkpStopPresentTimer(
    _In_ PDXGKRNL_ADAPTER Adapter);

/* ========================================================================
 * Function prototypes — present.c  (swap chain / present infrastructure)
 * ====================================================================== */

/*
 * DxgkPresentInit / DxgkPresentTeardown
 *
 * Per-adapter present queue lifecycle.  Called from DxgkAdapterStart /
 * DxgkAdapterStop in adapter.c.  present.h has the full queue structures;
 * these two are the only entry points visible to adapter.c.
 */
NTSTATUS
DxgkPresentInit(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkPresentTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter);

/* ========================================================================
 * Function prototypes — d3dkmt.c  (D3DKMT IOCTL dispatch table)
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

NTSTATUS
NTAPI
DxgkDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

NTSTATUS
NTAPI
DxgkDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

/*
 * D3DKMT entry points — adapter discovery and information.
 *
 * DxgkEnumAdapters, DxgkEnumAdapters2, DxgkOpenAdapterFromLuid,
 * DxgkCloseAdapter, and DxgkQueryAdapterInfo are implemented in d3dkmt.c
 * and called only from the IOCTL dispatch handler in the same file.
 * Their prototypes use types from d3dkmt.h (which provides Win8+ D3DKMT
 * types that are not available at the Vista interface version set globally)
 * and are therefore declared in d3dkmt.h / d3dkmt.c rather than here.
 */

/*
 * DxgkLookupAdapterByHandle
 *
 * Decode a D3DKMT_HANDLE and validate it against the global adapter list.
 * Returns the DXGKRNL_ADAPTER pointer if the handle is valid and the
 * adapter is in DxgkAdapterStateStarted; NULL otherwise.
 *
 * Used by vidpn.c (DxgkGetDisplayModeList) and other modules that need
 * to resolve user-mode adapter handles.
 */
PDXGKRNL_ADAPTER
DxgkLookupAdapterByHandle(
    _In_ D3DKMT_HANDLE Handle);

PDXGKRNL_DEVICE
DxgkLookupDeviceByHandle(
    _In_ D3DKMT_HANDLE      Handle,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter);

ULONG
NTAPI
DxgkAllocateSubmissionFenceId(
    _In_ PDXGKRNL_ADAPTER Adapter);

NTSTATUS
NTAPI
DxgkTrackSubmittedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG SubmissionFenceId,
    _In_ ULONG64 PresentId,
    _In_ PVOID Buffer,
    _In_ ULONG Tag,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_opt_ HANDLE SourceAllocationHandle,
    _In_opt_ HANDLE RefreshAllocationHandle,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID RefreshVidPnSourceId,
    _In_opt_ const RECT *RefreshDstRect,
    _In_reads_opt_(OpenHandleCount) CONST HANDLE *OpenHandles,
    _In_ UINT OpenHandleCount);

VOID
NTAPI
DxgkRetireCompletedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
NTAPI
DxgkReleaseTrackedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter);

PDXGKRNL_CONTEXT
DxgkLookupContextByHandle(
    _In_ D3DKMT_HANDLE      Handle,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_opt_ PDXGKRNL_DEVICE  *OutDevice);

/* ========================================================================
 * Function prototypes — legacy.c  (XDDM coexistence helpers)
 * ====================================================================== */

/*
 * DxgkLegacyDetect
 *
 * Returns TRUE if the adapter was created for an XDDM miniport (i.e.,
 * InitData.Version < DXGKDDI_INTERFACE_VERSION_VISTA).
 */
BOOLEAN
NTAPI
DxgkLegacyDetect(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkLegacyDetach
 *
 * Tears down an FDO that was mistakenly created for an XDDM miniport.
 * Returns STATUS_NOT_SUPPORTED so PnP retries with the next driver
 * (videoprt.sys).
 */
NTSTATUS
NTAPI
DxgkLegacyDetach(
    _In_ PDEVICE_OBJECT DeviceObject);

/* ========================================================================
 * Function prototypes — debug.c
 * ====================================================================== */

VOID
NTAPI
DxgkDebugInit(VOID);

#endif /* _DXGKRNL_PRIVATE_H_ */
