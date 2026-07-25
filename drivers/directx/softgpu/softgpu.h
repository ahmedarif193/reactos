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
 * allocates a 16 MB write-combined contiguous buffer from system RAM and
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

/* Framebuffer size: 16 MB (sufficient for 1920x1080x32bpp = ~8 MB) */
#define SOFTGPU_FB_SIZE         (16 * 1024 * 1024)

/* =========================================================================
 * SOFTGPU_CMD — the KMD DMA command stream format
 *
 * Every DMA buffer softgpu executes is a sequence of SOFTGPU_CMD records.
 * Address fields are patched at kick time (DxgkDdiPatch) with the absolute
 * physical placement of the referenced allocation plus AllocationOffset.
 * The execution engine validates every record against the VRAM slab before
 * touching memory; unrecognized DMA content completes as a no-op.
 * ========================================================================= */

#define SOFTGPU_CMD_MAGIC       0x444D4753UL    /* 'SGMD' */

#define SOFTGPU_CMD_OP_NOP          1
#define SOFTGPU_CMD_OP_BLT          2
#define SOFTGPU_CMD_OP_FILL         3
/* Paging: one linear move between the slab and a system-memory backing.
 * SlabAddress is a slab physical address; SystemAddress is the kernel VA of
 * the MDL dxgkrnl supplied for the backing.  ToSlab selects the direction. */
#define SOFTGPU_CMD_OP_PAGE         4
/* Paging: linear pattern fill of a slab range. */
#define SOFTGPU_CMD_OP_FILL_LINEAR  5

#define SOFTGPU_CMD_FLAG_TO_SLAB    0x00000001UL

typedef struct _SOFTGPU_CMD
{
    ULONG       Magic;
    ULONG       Op;
    ULONG       Size;
    ULONG       Color;
    RECT        SrcRect;
    RECT        DstRect;
    ULONG       SrcPitch;
    ULONG       DstPitch;
    ULONGLONG   SrcAddress;
    ULONGLONG   DstAddress;
    ULONGLONG   SlabAddress;
    ULONGLONG   SystemAddress;
    ULONGLONG   ByteCount;
    ULONG       Flags;
    ULONG       Reserved;
} SOFTGPU_CMD, *PSOFTGPU_CMD;

#define SOFTGPU_SUBMIT_RING_SIZE 1024

typedef struct _SOFTGPU_SUBMIT
{
    PHYSICAL_ADDRESS DmaPhys;
    ULONG            StartOffset;
    ULONG            EndOffset;
    ULONG            Fence;
} SOFTGPU_SUBMIT, *PSOFTGPU_SUBMIT;

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
    volatile LONG       Stopped;
    BOOLEAN             DpcInitialized;

    KTIMER              VsyncTimer;
    KDPC                VsyncDpc;
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
 * SOFTGPU_OPENALLOC — per-device open binding for a shared allocation
 *
 * Returned in DXGK_OPENALLOCATIONINFO.hDeviceSpecificAllocation by
 * DxgkDdiOpenAllocation and freed by DxgkDdiCloseAllocation.
 * ========================================================================= */

#define SOFTGPU_OPENALLOC_MAGIC 0x4F504753UL    /* 'SGPO' */

typedef struct _SOFTGPU_OPENALLOC
{
    ULONG           Magic;          /* must equal SOFTGPU_OPENALLOC_MAGIC   */
    D3DKMT_HANDLE   hAllocation;    /* dxgkrnl allocation handle (in)       */
} SOFTGPU_OPENALLOC, *PSOFTGPU_OPENALLOC;


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
 * SOFTGPU_PROCESS — per-process miniport context (WDDM 2.0 GPU VA model)
 *
 * Returned as DXGKARG_CREATEPROCESS.hKmdProcess from DxgkDdiCreateProcess and
 * handed back verbatim to DxgkDdiDestroyProcess.  softgpu has no real GPU MMU,
 * so this is just a tracked cookie to give dxgkrnl a non-NULL miniport handle.
 * ========================================================================= */

#define SOFTGPU_PROCESS_MAGIC   0x32475053UL    /* 'SPG2' */
#define SOFTGPU_WDDM2_POOL_TAG  '2GfS'          /* 'SfG2' reversed          */

typedef struct _SOFTGPU_PROCESS
{
    ULONG   Magic;                  /* must equal SOFTGPU_PROCESS_MAGIC     */
    HANDLE  hDxgkProcess;           /* opaque dxgkrnl process handle (in)   */
} SOFTGPU_PROCESS, *PSOFTGPU_PROCESS;


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

NTSTATUS
APIENTRY
SoftGpuDdiRenderGdi(
    _In_    CONST HANDLE       hContext,
    _Inout_ DXGKARG_RENDERGDI *pRenderGdi);

/* EOF */
