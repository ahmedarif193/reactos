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
 * dxgkrnl.sys uses the Windows 7 kernel API declaration set.
 * Override both _WIN32_WINNT and NTDDI_VERSION before including any kernel
 * headers so that Win7-era declarations are visible.
 *
 * The separately selected DXGKDDI interface level is WDDM 2.0; this NT target
 * controls kernel declarations and does not lower that graphics contract.
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
 * The dxgkrnl target supplies WDDM 2.0 explicitly. Keep the Win7 fallback
 * for translation units that include this private header outside that target;
 * it is not the interface level advertised by the built dxgkrnl.sys.
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
#include <reactos/drivers/directx/dxgmms2.h>
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
#define TAG_DXGK_HANDLE     'HxgD'   /* DXGH - typed D3DKMT handle entry   */
#define TAG_DXGK_CAPTURE    'UxgD'   /* DXGU - captured user buffers       */
#define DXGKP_MAX_USER_PRIVATE_DATA (1024U * 1024U)
#define DXGKP_MAX_CAPTURE_ALLOCATIONS 4096U

NTSTATUS NTAPI DxgkpCopyFromUserBuffer(_Out_writes_bytes_(BufferSize) PVOID KernelBuffer, _In_reads_bytes_(BufferSize) CONST VOID *SourceBuffer, _In_ SIZE_T BufferSize, _In_ KPROCESSOR_MODE AccessMode);
NTSTATUS NTAPI DxgkpCopyToUserBuffer(_Out_writes_bytes_(BufferSize) PVOID DestinationBuffer, _In_reads_bytes_(BufferSize) CONST VOID *KernelBuffer, _In_ SIZE_T BufferSize, _In_ KPROCESSOR_MODE AccessMode);
NTSTATUS NTAPI DxgkpProbeOutputBuffer(_Out_writes_bytes_(BufferSize) PVOID DestinationBuffer, _In_ SIZE_T BufferSize, _In_ KPROCESSOR_MODE AccessMode);
NTSTATUS NTAPI DxgkpCaptureUserBuffer(_In_reads_bytes_opt_(BufferSize) CONST VOID *SourceBuffer, _In_ SIZE_T BufferSize, _In_ KPROCESSOR_MODE AccessMode, _In_ ULONG PoolTag, _Outptr_result_bytebuffer_maybenull_(BufferSize) PVOID *CapturedBuffer);
NTSTATUS NTAPI DxgkpCreateContextWithAccessMode(_Inout_ D3DKMT_CREATECONTEXT *CreateContext, _In_ KPROCESSOR_MODE EmbeddedBufferMode);
NTSTATUS NTAPI DxgkpCreateAllocationWithAccessMode(_Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation, _In_ KPROCESSOR_MODE EmbeddedBufferMode);
NTSTATUS NTAPI DxgkpCreateAllocation2WithAccessMode(_Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation, _In_ KPROCESSOR_MODE EmbeddedBufferMode);
NTSTATUS NTAPI DxgkpSetVidPnSourceOwnerWithAccessMode(_In_ D3DKMT_SETVIDPNSOURCEOWNER *SetVidPnSourceOwner, _In_ KPROCESSOR_MODE EmbeddedBufferMode);

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

typedef enum _DXGKRNL_DMA_BACKING_KIND
{
    DxgkDmaBackingInvalid = 0,
    DxgkDmaBackingContiguousMemory
} DXGKRNL_DMA_BACKING_KIND;

typedef struct _DXGKRNL_DMA_BUFFER
{
    PVOID                       VirtualAddress;
    ULONG                       Capacity;
    ULONG                       SubmissionStartOffset;
    ULONG                       SubmissionEndOffset;
    UINT                        SegmentId;
    PHYSICAL_ADDRESS            SegmentAddress;
    DXGKRNL_DMA_BACKING_KIND    BackingKind;
} DXGKRNL_DMA_BUFFER, *PDXGKRNL_DMA_BUFFER;

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
    DxgkAdapterStateStarting,     /* Start is in progress; wait for completion */
    DxgkAdapterStateStarted,      /* DxgkDdiStartDevice succeeded             */
    DxgkAdapterStateStopping,     /* Stop admission and drain outstanding work */
    DxgkAdapterStateSurpriseRemoved, /* IRP_MN_SURPRISE_REMOVAL received       */
    DxgkAdapterStateRemoved,      /* IRP_MN_REMOVE_DEVICE received             */
} DXGKRNL_ADAPTER_STATE;

typedef enum _DXGKRNL_MMS2_ADAPTER_STATE
{
    DxgkMms2AdapterAbsent = 0,
    DxgkMms2AdapterCreated,
    DxgkMms2AdapterStarted,
    DxgkMms2AdapterBeginPending,
    DxgkMms2AdapterStopping,
    DxgkMms2AdapterStopped
} DXGKRNL_MMS2_ADAPTER_STATE;

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

    /* TRUE for the in-box basic-display fallback miniport (softgpu) —
     * the ReactOS equivalent of Windows' MSBDD.  The fallback only holds
     * the boot display until a real miniport acquires POST display
     * ownership, at which point dxgkrnl stops it (see
     * DxgkCbAcquirePostDisplayOwnership).  OS-side policy knob only; no
     * miniport-visible contract is attached to it. */
    BOOLEAN                     IsBasicDisplayFallback;

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
    PDXGKRNL_DMA_BUFFER         DmaBuffer;
    PDXGKRNL_DEVICE             Device;
    PDXGKRNL_CONTEXT            Context;
    HANDLE                      SourceAllocationHandle;
    HANDLE                      RefreshAllocationHandle;
    PDXGKVMM_ALLOCATION         SourceAllocation;
    PDXGKVMM_ALLOCATION         RefreshAllocation;
    PDXGKVMM_ALLOCATION         SourceOpenBindingReference;
    PDXGKVMM_ALLOCATION         DestinationOpenBindingReference;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID RefreshVidPnSourceId;
    RECT                        RefreshDstRect;
    BOOLEAN                     RefreshSharedPrimaryOnRetire;
    BOOLEAN                     SharedSurfaceRundownHeld;
    ULONG64                     SharedSurfaceGeneration;
    BOOLEAN                     SourceIsSharedPrimary;
    BOOLEAN                     SourceIsSharedShadow;
    ULONG                       SourceWidth;
    ULONG                       SourceHeight;
    ULONG                       SourcePitch;
    ULONG                       RefreshWidth;
    ULONG                       RefreshHeight;
    PHANDLE                     OpenHandleList;
    UINT                        OpenHandleCount;
    ULONG                       NodeOrdinal;
    ULONG                       EngineOrdinal;
    PDXGKRNL_ADAPTER            Adapter;
    BOOLEAN                     CloseOpenHandlesOnCancel;
    BOOLEAN                     ReservationActive;
    BOOLEAN                     FenceIdentityOwned;

    /* Optional monitored-fence signal fired when this submission's
     * GPU fence retires (WDDM signal-on-completion semantics for
     * hardware without GPU-writable fence values). */
    D3DKMT_HANDLE               hSignalSyncObject;
    ULONG64                     SignalFenceValue;
} DXGKRNL_SUBMIT_DMA_BUFFER, *PDXGKRNL_SUBMIT_DMA_BUFFER;

/* Per-node completed-fence tracking cap (independent GPU engine queues
 * can complete out of global fence order). */
#define DXGK_MAX_TRACKED_NODES 8
#define DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY 8192
#define DXGK_SUBMITTED_FENCE_PUBLISHED_BIT 0x8000000000000000ULL
#define DXGK_SUBMITTED_FENCE_TOMBSTONE ((LONG64)-1)

#define DXGKP_TDR_LEVEL_OFF 0
#define DXGKP_TDR_LEVEL_BUGCHECK 1
#define DXGKP_TDR_LEVEL_RECOVER_VGA 2
#define DXGKP_TDR_LEVEL_RECOVER 3
#define DXGKP_TDR_DEBUG_BREAK 0
#define DXGKP_TDR_DEBUG_IGNORE_TIMEOUT 1
#define DXGKP_TDR_DEBUG_RECOVER_NO_PROMPT 2
#define DXGKP_TDR_DEBUG_RECOVER_UNCONDITIONAL 3
#define DXGKP_TDR_HISTORY_CAPACITY 64

typedef struct _TDR_CONFIG
{
    ULONG TdrDelay;
    ULONG TdrDdiDelay;
    ULONG TdrLevel;
    ULONG TdrLimitCount;
    ULONG TdrLimitTime;
    ULONG TdrDebugMode;
    ULONG TdrTestMode;
} TDR_CONFIG, *PTDR_CONFIG;

extern TDR_CONFIG g_TdrConfig;

NTSTATUS NTAPI TdrCreateRecoveryContext(_Out_ PVOID *RecoveryContext, _In_ PVOID AdapterContext);
NTSTATUS NTAPI TdrCompleteRecoveryContext(_In_opt_ PVOID RecoveryContext);

/* Per-device in-flight submission budget (1.6): a single device may not
 * occupy the scheduler queue with more than this many un-retired
 * submissions; excess submissions get STATUS_DEVICE_BUSY. */
#define DXGK_DEVICE_MAX_INFLIGHT 256
/* Aggregate cap across ALL devices a process owns on one adapter. */
#define DXGK_PROCESS_MAX_INFLIGHT 512

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

    /* Closed immediately before the final DxgkDdiRemoveDevice callback.
     * Teardown paths must release OS tracking without calling the miniport
     * once this gate is closed. */
    KMUTEX                      MiniportCallbackMutex;
    volatile LONG               MiniportCallbacksValid;

    /* Nonblocking gate for ordinary KMD DDIs. Level-3 lifecycle and timeout
     * recovery serialize ordinary callbacks; the separate interrupt gate lets
     * ResetFromTimeout preserve the documented ISR/DPC execution contract. */
    KMUTEX                      KmdExclusiveMutex;
    KMUTEX                      KmdTransactionMutex;
    KMUTEX                      Level3TransitionMutex;
    volatile LONG               KmdCallsBlocked;
    volatile LONG               KmdActiveCalls;
    volatile PVOID              KmdExclusiveOwnerThread;
    volatile PVOID              KmdTransactionOwnerThread;
    volatile LONG               KmdTransactionDepth;
    volatile PVOID              Level3TransitionOwnerThread;
    volatile LONG               Level3TransitionDepth;
    volatile LONG               InterruptCallbacksBlocked;
    volatile LONG               InterruptActiveCalls;

    /* Counts physically live VidMm allocations/resources, including deferred shared backings. */
    volatile LONG               VidMmBackingCount;
    KEVENT                      VidMmBackingsDrainedEvent;
    volatile LONG               VidMmDestroyWorkerCount;
    volatile LONG               VidMmDestroyQueuesBlocked;
    KEVENT                      VidMmDestroyWorkersDrainedEvent;

    /* Current adapter lifecycle state. */
    DXGKRNL_ADAPTER_STATE       State;

    /* Administrative dxgmms2 ownership; AdapterMutex protects state/reason. */
    DXGMMS2_ADAPTER_HANDLE      Mms2Adapter;
    DXGKRNL_MMS2_ADAPTER_STATE Mms2State;
    DXGMMS2_STOP_REASON         Mms2StopReason;
    DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Mms2Timeline;
    volatile LONG               Mms2TimelineValid;
    volatile LONG               Mms2TimelineCallsOpen;
    volatile LONG               Mms2TimelineActiveCalls;

    /* Serializes start against stop, remove, and boot-display handover. */
    KEVENT                      AdapterStartCompletedEvent;
    ULONG                       AdapterStartGeneration;
    ULONG                       AdapterStartCompletedGeneration;
    NTSTATUS                    AdapterStartStatus;

    /* Serializes concurrent PnP and boot-display handover stop requests. */
    KEVENT                      AdapterStopCompletedEvent;
    volatile LONG               AdapterStopInProgress;
    ULONG                       AdapterStopGeneration;
    ULONG                       AdapterStopCompletedGeneration;
    LONG                        AdapterStopIntentCount;
    NTSTATUS                    AdapterStopStatus;

    /* Blocks new user-mode work and drains references before stop/remove. */
    EX_RUNDOWN_REF              RundownRef;
    volatile LONG               RundownStarted;

    /* Device create/destroy owners that may temporarily hold a device off
     * DeviceListHead. Stop closes admission, drains these owners, and only
     * then takes its final device-list snapshot. */
    volatile LONG               DeviceLifecycleActiveOperations;
    KEVENT                      DeviceLifecycleOperationsDrainedEvent;

    /* Pins the FDO, lower stack pointer, and callback context until remove. */
    EX_RUNDOWN_REF              RemoveRundownRef;
    volatile LONG               RemoveRundownStarted;

    /* Pins adapter-owned state used by reverse callbacks through the final
     * DxgkDdiRemoveDevice call. */
    EX_RUNDOWN_REF              ReverseCallbackRundownRef;
    volatile LONG               ReverseCallbackRundownStarted;

    /* TRUE only after the final DxgkDdiRemoveDevice ownership boundary. */
    BOOLEAN                     MiniportRemoveDeviceComplete;

    /*
     * Adapter LUID assigned during DxgkAdapterStart.
     * Used by D3DKMTEnumAdapters / D3DKMTOpenAdapterFromLuid to identify
     * adapters from user mode.
     */
    LUID                        AdapterLuid;

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

    /* Stable head fields cached from DXGKQAITYPE_DRIVERCAPS. */
    PHYSICAL_ADDRESS            HighestAcceptableAddress;
    DXGK_SCHEDULINGCAPS         SchedulingCaps;

    /* Cached while hardware is present.  A running-device surprise-removal
     * IRP must not query capabilities after the adapter has disappeared. */
    BOOLEAN                     SupportSurpriseRemoval;
    BOOLEAN                     SurpriseRemovalHandled;

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
    KMUTEX                     AdapterMutex;

    /* Serializes PASSIVE-level shared-primary/shadow lazy lifecycle. Readers
     * retain one coherent generation through SharedSurfaceRundown; writers
     * drain it before replacing handles, geometry, or ShadowFb backing. */
    KMUTEX                     SharedPrimaryMutex;
    EX_RUNDOWN_REF              SharedSurfaceRundown;
    ULONG64                     SharedSurfaceGeneration;
    ULONG                       SharedSurfaceMutationDepth;
    volatile LONG               SharedSurfaceAvailable;

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
    BOOLEAN                     ShadowFbPoolOwned;

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
     * dwm's vblank pacing event, registered by win32k via
     * IOCTL_VIDEO_DXGK_REGISTER_VBLANK (win32k owns the reference and
     * unregisters before releasing it). The present timer DPC signals it
     * every scanout period.
     */
    PKEVENT                     DwmVblankEvent;

    /*
     * Hardware-pointer bridge state: the display driver's XPDM pointer
     * IOCTLs (IOCTL_VIDEO_SET_POINTER_ATTR/POSITION/ENABLE/DISABLE) are
     * translated onto the miniport's DxgkDdiSetPointerShape/Position DDIs
     * in display.c.  Capabilities come from the miniport's
     * DxgkDdiQueryAdapterInfo(DXGKQAITYPE_DRIVERCAPS), queried once.
     * All state is written from the display IOCTL dispatch at PASSIVE_LEVEL.
     */
    BOOLEAN                     PointerCapsQueried;
    BOOLEAN                     PointerHwSupported;
    ULONG                       PointerMaxWidth;
    ULONG                       PointerMaxHeight;
    LONG                        PointerX;
    LONG                        PointerY;
    BOOLEAN                     PointerVisible;
    BOOLEAN                     PointerShapeValid;

    /*
     * Set when the miniport was already stopped through
     * DxgkDdiStopDeviceAndReleasePostDisplayOwnership (boot display
     * handover).  DxgkAdapterStop then skips the duplicate
     * DxgkDdiStopDevice call.
     */
    BOOLEAN                     MiniportDeviceStopped;

    /*
     * TDR watchdog: a 1 Hz timer watches the oldest tracked submission;
     * when it stops making progress for DXGKP_TDR_STUCK_TICKS ticks a
     * work item drives DxgkDdiResetFromTimeout/RestartFromTimeout.
     */
    KTIMER                      TdrTimer;
    KDPC                        TdrDpc;
    WORK_QUEUE_ITEM             TdrWorkItem;
    volatile LONG               TdrWorkQueued;
    ULONG                       TdrWorkFence;
    ULONG                       TdrWorkNode;
    ULONG                       TdrWorkEngine;
    ULONG                       TdrLastObservedFence;
    ULONG                       TdrLastObservedNode;
    ULONG                       TdrLastObservedEngine;
    ULONG                       TdrLastObservedCompletedFence;
    ULONG                       TdrStuckTicks;
    ULONGLONG                   TdrLastProgressTime100ns;
    volatile LONG               TdrTimerActive;
    volatile LONG               TdrOwnershipUncertain;
    volatile LONG               TdrCompletionNotificationsEnabled;
    TDR_CONFIG                  TdrConfig;
    KSPIN_LOCK                  TdrHistoryLock;
    ULONGLONG                   TdrRecoveryTimestamps[DXGKP_TDR_HISTORY_CAPACITY];
    ULONG                       TdrRecoveryWriteIndex;
    ULONG                       TdrRecoveryEntryCount;
    KTIMER                      TdrDdiTimer;
    KDPC                        TdrDdiDpc;
    volatile LONG               TdrDdiTimerArmed;
    PVOID                       TdrRecoveryContext;

    /*
     * Vblank pacing: miniport CRTC_VSYNC notifications (enabled through
     * DxgkDdiControlInterrupt at adapter start) set VsyncPending from the
     * "ISR"; the adapter DPC turns each pulse into a pending-dirty-rect
     * flush so presents pace to the scanout instead of the fallback timer.
     */
    volatile LONG               VsyncPending;

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
    /* A dynamically allocated worker owns this active flag. */
    volatile LONG               SubmitDmaRetireWorkQueued;
    volatile LONG               SubmitDmaRetireActiveWorkers;
    KEVENT                      SubmitDmaRetireDrainedEvent;
    volatile LONG               SubmitDmaStopping;
    volatile LONG               SubmitDmaActiveReservations;
    KEVENT                      SubmitDmaReservationsDrainedEvent;
    volatile LONG               NextSubmissionFenceId;
    volatile ULONG              LastCompletedSubmissionFenceId;
    volatile ULONG              NodeLastSubmittedFenceId[DXGK_MAX_TRACKED_NODES];
    volatile ULONG              NodeLastCompletedFenceId[DXGK_MAX_TRACKED_NODES];
    volatile LONG64             SubmittedFenceIdentities[DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY];

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
    volatile LONG               PresentQueueStopping;
    volatile LONG               PresentQueueActiveCalls;
    KEVENT                      PresentQueueCallsDrainedEvent;

    /*
     * Video Scheduler (VidSch) context for this adapter.
     * Typed PVOID to avoid pulling vidsch.h into this header; cast to
     * PVIDSCH_CONTEXT by vidsch.c callers.  NULL for DOD adapters or
     * until VidSchInitialize is called.
     */
    PVOID                       VidSchContext;       /* PVIDSCH_CONTEXT */
    volatile LONG               VidSchStopping;
    volatile LONG               VidSchActiveCalls;

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

C_ASSERT((FIELD_OFFSET(DXGKRNL_ADAPTER, SubmittedFenceIdentities) & (sizeof(LONG64) - 1)) == 0);
C_ASSERT((DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY & (DXGK_SUBMITTED_FENCE_IDENTITY_CAPACITY - 1)) == 0);

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

    /* Owner-scoped typed generation handle returned to user mode. */
    D3DKMT_HANDLE               Handle;

    /* Creation flags (D3DKMT_CREATEDEVICEFLAGS). */
    D3DKMT_CREATEDEVICEFLAGS    Flags;

    /* Per-device GPU budget: submissions in flight (tracked DMA buffers
     * not yet retired).  Bounds one device's queue occupancy. */
    volatile LONG               InFlightSubmissions;

    /* Persistent KMT execution result. TDR marks existing devices RESET; new
     * devices start ACTIVE and stopped adapters are reported as STOPPED. */
    volatile LONG               ExecutionState;

    /* Creating process (identity only, for per-process budget
     * aggregation across this process's devices). */
    PEPROCESS                   OwnerProcess;

    /* Shared per-process/per-adapter WDDM 2.0 GPU state. */
    PDXGKRNL_PROCESS            ProcessRecord;

    /* Destroying closes admission; TeardownClaimed elects one final owner. */
    volatile LONG               ReferenceCount;
    volatile LONG               Destroying;
    volatile LONG               TeardownClaimed;
    volatile LONG               TeardownReferencesDrained;
    volatile LONG               MiniportDestroyPending;
    volatile LONG               TeardownOsCleanupComplete;
    KEVENT                      ReferencesDrainedEvent;

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

    /* Owner-scoped typed generation handle returned to user mode. */
    D3DKMT_HANDLE               Handle;

    /*
     * GPU engine routing supplied by the caller at creation time.
     *   NodeOrdinal    — zero-based GPU node index.
     *   EngineAffinity — bitmask of engines within that node.
     */
    UINT                        NodeOrdinal;
    UINT                        EngineAffinity;
    INT                         SchedulingPriority;

    /* TRUE when created through D3DKMTCreateContextVirtual. */
    BOOLEAN                     VirtualAddressing;

    /* Original UMD flags; these are not bit-compatible with KMD context flags. */
    D3DDDI_CREATECONTEXTFLAGS   UserModeCreateFlags;

    /*
     * The list owns one reference. Virtual scheduler packets acquire an
     * additional reference until the hardware fence retires. Destruction
     * first removes the context from the handle namespace, then waits for
     * those packet references to drain before calling the miniport. The
     * retained device reference also drains a competing parent teardown.
     */
    volatile LONG               ReferenceCount;
    volatile LONG               Destroying;
    volatile LONG               TeardownClaimed;
    volatile LONG               TeardownReferencesDrained;
    volatile LONG               MiniportDestroyPending;
    KEVENT                      ReferencesDrainedEvent;

    /*
     * Miniport-side context handle returned from DxgkDdiCreateContext.
     */
    HANDLE                      hMiniportContext;

    /* DMA geometry and capabilities returned by DxgkDdiCreateContext. */
    DXGK_CONTEXTINFO            ContextInfo;

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

    /* Logical WDDM 2.x mapping protection retained for update/copy operations. */
    D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection;
    UINT64                      DriverProtection;

    /* Original reservation identity, preserved across logical range splits. */
    D3DGPU_VIRTUAL_ADDRESS      ReservationBase;
    ULONGLONG                   ReservationSize;

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
 * The WDDM 2.0 build keeps per-process GPU virtual-address state here.
 * ====================================================================== */
struct _DXGKRNL_PROCESS
{
    /* Referenced owning process object and shared-device reference count. */
    PEPROCESS                   Process;
    volatile LONG               ReferenceCount;

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

    /* TRUE only after dxgkrnl has built and submitted real GPU PTE updates. */
    BOOLEAN                     RootPageTableProgrammed;

    /*
     * GPU VA range list: sorted doubly-linked list of DXGKRNL_GPUVA_RANGE.
     * Protected by GpuVaLock.
     */
    LIST_ENTRY                  GpuVaRangeList;
    FAST_MUTEX                  GpuVaLock;
    ULONG                       GpuVaRangeCount;

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
    PDXGKRNL_DEVICE             Device;
    PEPROCESS                   OwnerProcess;
    D3DDDI_SYNCHRONIZATIONOBJECTINFO Info;
    D3DDDI_SYNCHRONIZATIONOBJECT_FLAGS Flags;
    volatile LONG               RefCount;
    volatile LONG               Destroying;
    /* Direct destroy and device cleanup race for this final-owner claim. */
    volatile LONG               TeardownClaimed;
    volatile LONG64             FenceValue;
    volatile LONG               TdrAffected;
    KEVENT                      CpuEvent;
    LIST_ENTRY                  SyncObjListEntry;
    LIST_ENTRY                  DeviceSyncObjListEntry;

    /*
     * WDDM 2.0 monitored fence: a nonpaged page holding the 64-bit fence
     * value, mapped read-only into the creating process so user mode can
     * poll it without an ioctl (D3DDDI_SYNCHRONIZATIONOBJECTINFO2
     * MonitoredFence.FenceValueCPUVirtualAddress).  Signal paths write the
     * page through MonitoredValueKernelVa.
     */
    PVOID                       MonitoredValueKernelVa;
    PMDL                        MonitoredValueMdl;
    PVOID                       MonitoredValueUserVa;
    PEPROCESS                   MonitoredValueProcess;
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
extern volatile LONG    GDxgControlDeviceState;

NTSTATUS
DxgkpEnsureControlDevice(VOID);

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

VOID
DxgkpInitializeCoreInterface(VOID);

NTSTATUS
DxgkpEnsureGlobalInitialization(VOID);

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

/* PASSIVE_LEVEL teardown-DDI gate. A TRUE acquire holds the mutex until the
 * matching release; FALSE means RemoveDevice owns or crossed the boundary. */
BOOLEAN
DxgkAcquireMiniportCallback(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkReleaseMiniportCallback(
    _In_ PDXGKRNL_ADAPTER Adapter);

BOOLEAN
DxgkAcquireKmdCall(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkReleaseKmdCall(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkBeginKmdExclusive(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkEndKmdExclusive(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN ReopenAdmission);

VOID DxgkpArmTdrDdiDeadline(_In_ PDXGKRNL_ADAPTER Adapter);
VOID DxgkpDisarmTdrDdiDeadline(_In_ PDXGKRNL_ADAPTER Adapter);

BOOLEAN
DxgkBeginKmdTransaction(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkEndKmdTransaction(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkAcquireLevel3Transition(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkReleaseLevel3Transition(
    _In_ PDXGKRNL_ADAPTER Adapter);

BOOLEAN
DxgkAcquireInterruptCallback(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkReleaseInterruptCallback(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkBlockInterruptCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkUnblockInterruptCallbacks(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkpQueryDriverCaps
 *
 * DXGKQAITYPE_DRIVERCAPS query into a caller buffer of
 * DXGKP_DRIVERCAPS_QUERY_SIZE bytes.  The generous size absorbs
 * WDK-size DXGK_DRIVERCAPS writes from miniports built against newer
 * headers (WDDM 2.x/3.x additions); only the head of the structure —
 * stable since Vista and mirrored exactly by our DXGK_DRIVERCAPS — may
 * be interpreted.
 */
#define DXGKP_DRIVERCAPS_QUERY_SIZE 1024

NTSTATUS
DxgkpQueryDriverCaps(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_writes_bytes_(DXGKP_DRIVERCAPS_QUERY_SIZE) PDXGK_DRIVERCAPS Caps);

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

PDXGKVMM_RESOURCE
DxgkVidMmCreateResourceWrapper(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_opt_ HANDLE          MiniportHandle,
    _In_ D3DKMT_HANDLE       GlobalShareHandle,
    _In_ BOOLEAN              Shareable,
    _In_reads_bytes_opt_(PrivateRuntimeDataSize)
            CONST VOID      *PrivateRuntimeData,
    _In_    UINT             PrivateRuntimeDataSize,
    _In_reads_bytes_opt_(ResourcePrivateDriverDataSize)
            CONST VOID      *ResourcePrivateDriverData,
    _In_    UINT             ResourcePrivateDriverDataSize);

NTSTATUS
DxgkVidMmAttachAllocationToResource(
    _In_ PDXGKVMM_RESOURCE Resource,
    _In_ PDXGKVMM_ALLOCATION Allocation);

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
DxgkCreateAllocation2(
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
    _In_  D3DGPU_VIRTUAL_ADDRESS   MinAddress,
    _In_  D3DGPU_VIRTUAL_ADDRESS   MaxAddress,
    _In_  ULONGLONG                SizeInBytes,
    _In_  D3DDDIGPUVIRTUALADDRESS_RESERVATION_TYPE ReservationType,
    _In_  UINT64                   DriverProtection,
    _Out_ D3DGPU_VIRTUAL_ADDRESS  *OutAddress);

NTSTATUS
DxgkGpuVaFree(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_ ULONGLONG              SizeInBytes);

BOOLEAN
DxgkGpuVaPageTableReady(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process);

BOOLEAN
DxgkGpuVaValidateRange(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size);

NTSTATUS
DxgkGpuVaValidateUpdate(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_reads_(NumOperations) CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operations,
    _In_ UINT NumOperations);

/*
 * GPU VA residency management (WDDM 2.0).
 */
NTSTATUS
DxgkGpuVaMakeResident(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_reads_(NumAllocations) CONST D3DKMT_HANDLE *AllocationList,
    _In_    ULONG              NumAllocations,
    _Out_   ULONG             *OutCompleted,
    _Out_   ULONGLONG         *OutNumBytesToTrim);

NTSTATUS
DxgkGpuVaEvict(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_reads_(NumAllocations) CONST D3DKMT_HANDLE *AllocationList,
    _In_    ULONG              NumAllocations,
    _In_    BOOLEAN            EvictOnlyIfNecessary,
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

#include "handles.h"

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
DxgkCreateContextVirtual(
    _Inout_ D3DKMT_CREATECONTEXTVIRTUAL *pCreateContext);

NTSTATUS
DxgkReferenceVirtualContextByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice,
    _Out_ PDXGKRNL_CONTEXT *OutContext);

NTSTATUS
DxgkReferenceOwnedDeviceByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice);

NTSTATUS
DxgkReferenceProcessRecordByAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Out_ PDXGKRNL_PROCESS *OutProcessRecord);

VOID
DxgkDereferenceProcessRecord(
    _In_opt_ PDXGKRNL_PROCESS ProcessRecord);

VOID
DxgkDereferenceContext(
    _In_ PDXGKRNL_CONTEXT Context);

BOOLEAN
DxgkReferenceContext(
    _In_ PDXGKRNL_CONTEXT Context);

BOOLEAN
DxgkReferenceDevice(
    _In_ PDXGKRNL_DEVICE Device);

VOID
DxgkDereferenceDevice(
    _In_ PDXGKRNL_DEVICE Device);

NTSTATUS
NTAPI
DxgkDestroyContext(
    _In_ D3DKMT_DESTROYCONTEXT *pDestroyContext);

VOID
DxgkD3dkmtProcessCleanup(
    _In_ PEPROCESS Process);

VOID
DxgkD3dkmtAdapterCleanup(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkD3dkmtDeviceCleanup(
    _In_ PDXGKRNL_DEVICE Device);

NTSTATUS
DxgkCleanupAdapterDevices(
    _In_ PDXGKRNL_ADAPTER Adapter);

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
DxgkSyncObjectGpuRetireSignal(
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue);

NTSTATUS
NTAPI
DxgkSyncObjectCpuWait(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue,
    _In_ BOOLEAN NonBlocking);

NTSTATUS
NTAPI
DxgkSyncObjectAttachMonitoredPage(
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 InitialFenceValue,
    _In_ D3DDDI_SYNCHRONIZATIONOBJECT_FLAGS Flags,
    _Out_ PVOID *UserVa);

VOID
NTAPI
DxgkTdrResetAdapterSynchronizationObjects(
    _In_ PDXGKRNL_ADAPTER Adapter);

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
 * DxgkDisplayVsyncFlush
 *
 * Called from the adapter DPC when the miniport reported a CRTC_VSYNC
 * pulse: flushes pending dirty rects so presents pace to the scanout.
 * DISPATCH_LEVEL-safe.
 */
VOID
DxgkDisplayVsyncFlush(
    _In_ PDXGKRNL_ADAPTER Adapter);

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

BOOLEAN
NTAPI
DxgkReserveSubmissionFenceIdentity(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId);

VOID NTAPI DxgkPublishSubmittedFence(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId);
BOOLEAN NTAPI DxgkIsSubmittedFenceIdentity(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId);
VOID NTAPI DxgkReleaseSubmittedFenceIdentity(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG SubmissionFenceId);
VOID NTAPI DxgkResetSubmittedFenceIdentities(_In_ PDXGKRNL_ADAPTER Adapter);
NTSTATUS NTAPI DxgkNotifySubmissionFenceCompletion(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId, _In_ BOOLEAN Preempted, _Out_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot);
VOID NTAPI DxgkDrainVidSchCallbacks(_In_ PDXGKRNL_ADAPTER Adapter);

/* Teardown-path wait: sleep (1 ms) until a worker-busy flag clears.
 * PASSIVE_LEVEL only. */
FORCEINLINE
VOID
DxgkpWaitForFlagClear(
    _In_ volatile LONG *Flag)
{
    while (InterlockedCompareExchange((volatile LONG *)Flag, 0, 0) != 0)
    {
        LARGE_INTEGER Delay;
        Delay.QuadPart = -10000; /* 1 ms */
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

/* Tracker reservation arguments: zero-init, then set what applies.
 * SubmissionFenceId and DmaBuffer are mandatory. */
typedef struct _DXGKRNL_TRACK_DMA_ARGS
{
    ULONG                           SubmissionFenceId;
    ULONG                           NodeOrdinal;
    ULONG                           EngineOrdinal;
    D3DKMT_HANDLE                   hSignalSyncObject;
    ULONG64                         SignalFenceValue;
    ULONG64                         PresentId;
    PDXGKRNL_DMA_BUFFER             DmaBuffer;
    PDXGKRNL_DEVICE                 Device;
    PDXGKRNL_CONTEXT                Context;
    PDXGKVMM_ALLOCATION             SourceAllocation;
    PDXGKVMM_ALLOCATION             RefreshAllocation;
    PDXGKVMM_ALLOCATION             SourceOpenBindingReference;
    PDXGKVMM_ALLOCATION             DestinationOpenBindingReference;
    HANDLE                          SourceAllocationHandle;
    HANDLE                          RefreshAllocationHandle;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  RefreshVidPnSourceId;
    const RECT                     *RefreshDstRect;
    ULONG64                         SharedSurfaceGeneration;
    BOOLEAN                         SourceIsSharedPrimary;
    BOOLEAN                         SourceIsSharedShadow;
    BOOLEAN                         RefreshIsSharedPrimary;
    BOOLEAN                         HoldSharedSurfaceRundown;
    ULONG                           SourceWidth;
    ULONG                           SourceHeight;
    ULONG                           SourcePitch;
    ULONG                           RefreshWidth;
    ULONG                           RefreshHeight;
    CONST HANDLE                   *OpenHandles;
    UINT                            OpenHandleCount;
} DXGKRNL_TRACK_DMA_ARGS, *PDXGKRNL_TRACK_DMA_ARGS;

NTSTATUS
NTAPI
DxgkAllocateDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Capacity,
    _Out_ PDXGKRNL_DMA_BUFFER *OutDmaBuffer);

VOID
NTAPI
DxgkFreeDmaBuffer(
    _In_opt_ PDXGKRNL_DMA_BUFFER DmaBuffer);

NTSTATUS
NTAPI
DxgkPrepareTrackedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const DXGKRNL_TRACK_DMA_ARGS *Args,
    _Out_ PDXGKRNL_SUBMIT_DMA_BUFFER *OutEntry);

VOID
NTAPI
DxgkCommitTrackedDmaBuffer(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry);

VOID
NTAPI
DxgkAdoptTrackedDmaBuffer(
    _In_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry);

VOID
NTAPI
DxgkCancelTrackedDmaBuffer(
    _In_opt_ PDXGKRNL_SUBMIT_DMA_BUFFER Entry);

VOID
NTAPI
DxgkRetireCompletedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
NTAPI
DxgkReleaseTrackedDmaBuffers(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN MiniportCallbacksValid);

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
