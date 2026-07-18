/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel-mode DDI structures for WDDM display miniport drivers.
 *              Defines DXGKARG_*, DXGK_SEGMENTDESCRIPTOR, DXGK_DRIVERCAPS,
 *              DXGK_QUERYADAPTERINFOTYPE, DXGK_INTERRUPT_TYPE and all
 *              related types consumed by the DXGKDDI_* callbacks.
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *
 * REFERENCES:
 *   [WDK81]  d3dkmddi.h from the Windows 8.1 WDK
 *   [MSWDK]  Windows Driver Kit - Display Drivers (WDDM)
 *            https://docs.microsoft.com/en-us/windows-hardware/drivers/display/
 */

#ifndef _D3DKMDDI_H_
#define _D3DKMDDI_H_

#include <d3dkmdt.h>

#pragma warning(push)
#pragma warning(disable:4201) /* nameless struct/union */
#pragma warning(disable:4200) /* zero-sized array     */
#pragma warning(disable:4214) /* bit-field type other than int */

typedef struct _DXGK_ALLOCATIONINFOFLAGS_WDDM2_0
{
    union
    {
        struct
        {
            UINT    CpuVisible                      : 1;
            UINT    PermanentSysMem                 : 1;
            UINT    Cached                          : 1;
            UINT    Protected                       : 1;
            UINT    ExistingSysMem                  : 1;
            UINT    ExistingKernelSysMem            : 1;
            UINT    FromEndOfSegment                : 1;
            UINT    DisableLargePageMapping         : 1;
            UINT    Overlay                         : 1;
            UINT    Capture                         : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
            UINT    CreateInVpr                     : 1;
#else
            UINT    Reserved00                      : 1;
#endif
            UINT    DXGK_ALLOC_RESERVED17           : 1;
            UINT    Reserved02                      : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
            UINT    MapApertureCpuVisible           : 1;
#else
            UINT    Reserved03                      : 1;
#endif
            UINT    HistoryBuffer                   : 1;
            UINT    AccessedPhysically              : 1;
            UINT    ExplicitResidencyNotification   : 1;
            UINT    HardwareProtected               : 1;
            UINT    CpuVisibleOnDemand              : 1;
            UINT    DXGK_ALLOC_RESERVED16           : 1;
            UINT    DXGK_ALLOC_RESERVED15           : 1;
            UINT    DXGK_ALLOC_RESERVED14           : 1;
            UINT    DXGK_ALLOC_RESERVED13           : 1;
            UINT    DXGK_ALLOC_RESERVED12           : 1;
            UINT    DXGK_ALLOC_RESERVED11           : 1;
            UINT    DXGK_ALLOC_RESERVED10           : 1;
            UINT    DXGK_ALLOC_RESERVED9            : 1;
            UINT    DXGK_ALLOC_RESERVED4            : 1;
            UINT    DXGK_ALLOC_RESERVED3            : 1;
            UINT    DXGK_ALLOC_RESERVED2            : 1;
            UINT    DXGK_ALLOC_RESERVED1            : 1;
            UINT    DXGK_ALLOC_RESERVED0            : 1;
        };
        UINT Value;
    };
} DXGK_ALLOCATIONINFOFLAGS_WDDM2_0;
C_ASSERT(sizeof(DXGK_ALLOCATIONINFOFLAGS_WDDM2_0) == 0x4);

typedef struct _DXGK_SEGMENTPREFERENCE
{
    union
    {
        struct
        {
            UINT SegmentId0 : 5;
            UINT Direction0 : 1;
            UINT SegmentId1 : 5;
            UINT Direction1 : 1;
            UINT SegmentId2 : 5;
            UINT Direction2 : 1;
            UINT SegmentId3 : 5;
            UINT Direction3 : 1;
            UINT SegmentId4 : 5;
            UINT Direction4 : 1;
            UINT Reserved   : 2;
        };
        UINT Value;
    };
} DXGK_SEGMENTPREFERENCE, *PDXGK_SEGMENTPREFERENCE;
C_ASSERT(sizeof(DXGK_SEGMENTPREFERENCE) == 0x4);

typedef struct _DXGK_SEGMENTBANKPREFERENCE
{
    union
    {
        struct
        {
            UINT Bank0          : 7;
            UINT Direction0     : 1;
            UINT Bank1          : 7;
            UINT Direction1     : 1;
            UINT Bank2          : 7;
            UINT Direction2     : 1;
            UINT Bank3          : 7;
            UINT Direction3     : 1;
        };
        UINT Value;
    };
} DXGK_SEGMENTBANKPREFERENCE;
C_ASSERT(sizeof(DXGK_SEGMENTBANKPREFERENCE) == 0x4);

typedef struct _DXGK_ALLOCATIONINFOFLAGS
{
    union
    {
        struct
        {
            UINT CpuVisible              : 1;
            UINT PermanentSysMem         : 1;
            UINT Cached                  : 1;
            UINT Protected               : 1;
            UINT ExistingSysMem          : 1;
            UINT ExistingKernelSysMem    : 1;
            UINT FromEndOfSegment        : 1;
            UINT Swizzled                : 1;
            UINT Overlay                 : 1;
            UINT Capture                 : 1;
            UINT UseAlternateVA          : 1;
            UINT SynchronousPaging       : 1;
            UINT LinkMirrored            : 1;
            UINT LinkInstanced           : 1;
            UINT HistoryBuffer           : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
            UINT AccessedPhysically      : 1;
            UINT ExplicitResidencyNotification : 1;
            UINT HardwareProtected       : 1;
            UINT CpuVisibleOnDemand      : 1;
#else
            UINT Reserved                : 4;
#endif
            UINT DXGK_ALLOC_RESERVED16   : 1;
            UINT DXGK_ALLOC_RESERVED15   : 1;
            UINT DXGK_ALLOC_RESERVED14   : 1;
            UINT DXGK_ALLOC_RESERVED13   : 1;
            UINT DXGK_ALLOC_RESERVED12   : 1;
            UINT DXGK_ALLOC_RESERVED11   : 1;
            UINT DXGK_ALLOC_RESERVED10   : 1;
            UINT DXGK_ALLOC_RESERVED9    : 1;
            UINT DXGK_ALLOC_RESERVED4    : 1;
            UINT DXGK_ALLOC_RESERVED3    : 1;
            UINT DXGK_ALLOC_RESERVED2    : 1;
            UINT DXGK_ALLOC_RESERVED1    : 1;
            UINT DXGK_ALLOC_RESERVED0    : 1;
        };
        UINT Value;
    };
} DXGK_ALLOCATIONINFOFLAGS;
C_ASSERT(sizeof(DXGK_ALLOCATIONINFOFLAGS) == 0x4);

typedef struct _DXGK_ALLOCATIONUSAGEINFO1
{
    union
    {
        struct
        {
            UINT PrivateFormat  : 1;
            UINT Swizzled       : 1;
            UINT MipMap         : 1;
            UINT Cube           : 1;
            UINT Volume         : 1;
            UINT Vertex         : 1;
            UINT Index          : 1;
            UINT Reserved       : 25;
        };
        UINT Value;
    } Flags;
    union
    {
        D3DDDIFORMAT Format;
        UINT         PrivateFormat;
    };
    UINT SwizzledFormat;
    UINT ByteOffset;
    UINT Width;
    UINT Height;
    UINT Pitch;
    UINT Depth;
    UINT SlicePitch;
} DXGK_ALLOCATIONUSAGEINFO1;
C_ASSERT(sizeof(DXGK_ALLOCATIONUSAGEINFO1) == 0x24);

typedef struct _DXGK_ALLOCATIONUSAGEHINT
{
    UINT                      Version;
    DXGK_ALLOCATIONUSAGEINFO1 v1;
} DXGK_ALLOCATIONUSAGEHINT;
C_ASSERT(sizeof(DXGK_ALLOCATIONUSAGEHINT) == 0x28);

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
typedef struct _DXGK_ALLOCATIONINFOFLAGS2
{
    union
    {
        struct
        {
            UINT    CpuVisibleOnDemand      : 1;    // 0x00000001
            UINT    Reserved                :31;    // 0xFFFFFFFE
        };
        UINT Value;
    };
} DXGK_ALLOCATIONINFOFLAGS2;
#endif // DXGKDDI_INTERFACE_VERSION_WDDM3_0

typedef struct _DXGK_ALLOCATIONINFO
{
    VOID*                      pPrivateDriverData;
    UINT                       PrivateDriverDataSize;
    UINT                       Alignment;
    SIZE_T                     Size;
    SIZE_T                     PitchAlignedSize;
    DXGK_SEGMENTBANKPREFERENCE HintedBank;
    DXGK_SEGMENTPREFERENCE     PreferredSegment;
    UINT                       SupportedReadSegmentSet;
    UINT                       SupportedWriteSegmentSet;
    UINT                       EvictionSegmentSet;
    union
    {
        UINT MaximumRenamingListLength;
        UINT PhysicalAdapterIndex;
    };
    HANDLE hAllocation;
    union
    {
        DXGK_ALLOCATIONINFOFLAGS Flags;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        DXGK_ALLOCATIONINFOFLAGS_WDDM2_0 FlagsWddm2;
#endif
    };
    DXGK_ALLOCATIONUSAGEHINT* pAllocationUsageHint;
    UINT                      AllocationPriority;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
    DXGK_ALLOCATIONINFOFLAGS2 Flags2;
#endif
} DXGK_ALLOCATIONINFO;

#ifdef _WIN64
/* Flags2 occupies the 64-bit tail hole after AllocationPriority, so the
 * 64-bit size does not grow at WDDM 3.0 (the 32-bit size does). */
C_ASSERT(sizeof(DXGK_ALLOCATIONINFO) == 0x58);
#else
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
C_ASSERT(sizeof(DXGK_ALLOCATIONINFO) == 0x40);
#else
C_ASSERT(sizeof(DXGK_ALLOCATIONINFO) == 0x3C);
#endif
#endif


/* =========================================================================
 * DXGK_INTERRUPT_TYPE
 *
 * Identifies the GPU event type carried by DXGKARGCB_NOTIFY_INTERRUPT_DATA.
 * The #define constants in dispmprt.h (DXGK_INTERRUPT_DMA_COMPLETED etc.)
 * correspond to these enum values.
 * =========================================================================
 */
typedef enum _DXGK_INTERRUPT_TYPE
{
    DXGK_INTERRUPT_DMA_COMPLETED = 1,
    DXGK_INTERRUPT_DMA_PREEMPTED = 2,
    DXGK_INTERRUPT_CRTC_VSYNC = 3,
    DXGK_INTERRUPT_DMA_FAULTED = 4,
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    DXGK_INTERRUPT_DISPLAYONLY_VSYNC = 5,
    DXGK_INTERRUPT_DISPLAYONLY_PRESENT_PROGRESS = 6,
    DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY = 7,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    DXGK_INTERRUPT_MICACAST_CHUNK_PROCESSING_COMPLETE = 8,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    DXGK_INTERRUPT_DMA_PAGE_FAULTED = 9,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2 = 10,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    DXGK_INTERRUPT_MONITORED_FENCE_SIGNALED = 11,
    DXGK_INTERRUPT_HWQUEUE_PAGE_FAULTED = 12,
    DXGK_INTERRUPT_HWCONTEXTLIST_SWITCH_COMPLETED = 13,
    DXGK_INTERRUPT_PERIODIC_MONITORED_FENCE_SIGNALED = 14,
#endif
} DXGK_INTERRUPT_TYPE;

#define DXGK_INTERRUPT_TYPE_DMA_COMPLETED DXGK_INTERRUPT_DMA_COMPLETED
#define DXGK_INTERRUPT_TYPE_DMA_PREEMPTED DXGK_INTERRUPT_DMA_PREEMPTED
#define DXGK_INTERRUPT_TYPE_CRTC_VSYNC DXGK_INTERRUPT_CRTC_VSYNC
#define DXGK_INTERRUPT_TYPE_DMA_FAULTED DXGK_INTERRUPT_DMA_FAULTED
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
#define DXGK_INTERRUPT_TYPE_DISPLAYONLY_VSYNC DXGK_INTERRUPT_DISPLAYONLY_VSYNC
#define DXGK_INTERRUPT_TYPE_DISPLAYONLY_PRESENT_PROGRESS DXGK_INTERRUPT_DISPLAYONLY_PRESENT_PROGRESS
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
#define DXGK_INTERRUPT_TYPE_PERIODICED_MONITORED_FENCE_SIGNALED DXGK_INTERRUPT_PERIODIC_MONITORED_FENCE_SIGNALED
#endif


/* =========================================================================
 * DXGK_QUERYADAPTERINFOTYPE
 *
 * Selects which piece of adapter information DxgkDdiQueryAdapterInfo
 * should return.
 * =========================================================================
 */
/* Values match the genuine WDK d3dkmddi.h (winsdk-10 10.0.16299) exactly —
 * miniports built against the real WDK dispatch on these numbers (the old
 * hand-authored values, e.g. QUERYSEGMENT3=8, collided with other types). */
typedef enum _DXGK_QUERYADAPTERINFOTYPE
{
    DXGKQAITYPE_UMDRIVERPRIVATE           = 0,
    DXGKQAITYPE_DRIVERCAPS                = 1,
    DXGKQAITYPE_QUERYSEGMENT              = 2,
    DXGKQAITYPE_RESERVED                  = 3,
    DXGKQAITYPE_QUERYSEGMENT2             = 4,
    DXGKQAITYPE_QUERYSEGMENT3             = 5,
    DXGKQAITYPE_NUMPOWERCOMPONENTS        = 6,
    DXGKQAITYPE_POWERCOMPONENTINFO        = 7,
    DXGKQAITYPE_PREFERREDGPUNODE          = 8,
    DXGKQAITYPE_POWERCOMPONENTPSTATEINFO  = 9,
    DXGKQAITYPE_HISTORYBUFFERPRECISION    = 10,
    DXGKQAITYPE_QUERYSEGMENT4             = 11,
    DXGKQAITYPE_SEGMENTMEMORYSTATE        = 12,
    DXGKQAITYPE_GPUMMUCAPS                = 13,
    DXGKQAITYPE_PAGETABLELEVELDESC        = 14,
    DXGKQAITYPE_PHYSICALADAPTERCAPS       = 15,
    DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION  = 16,
    DXGKQAITYPE_INTEGRATED_DISPLAY_DESCRIPTOR = 17,
    DXGKQAITYPE_UEFIFRAMEBUFFERRANGES         = 18,
    DXGKQAITYPE_QUERYCOLORIMETRYOVERRIDES     = 19,
    DXGKQAITYPE_DISPLAYID_DESCRIPTOR          = 20,
} DXGK_QUERYADAPTERINFOTYPE;


/* =========================================================================
 * DXGKARG_QUERYADAPTERINFO
 * =========================================================================
 */
typedef struct _DXGKARG_QUERYADAPTERINFO
{
    DXGK_QUERYADAPTERINFOTYPE   Type;
    PVOID                       pInputData;
    UINT                        InputDataSize;
    PVOID                       pOutputData;
    UINT                        OutputDataSize;
} DXGKARG_QUERYADAPTERINFO, *PDXGKARG_QUERYADAPTERINFO;


/* =========================================================================
 * DXGK_POINTERFLAGS  -  hardware cursor capabilities
 * =========================================================================
 */
typedef struct _DXGK_POINTERFLAGS
{
    union
    {
        struct
        {
            UINT    Monochrome      : 1;
            UINT    Color           : 1;
            UINT    MaskedColor     : 1;
            UINT    Reserved        : 29;
        };
        UINT    Value;
    };
} DXGK_POINTERFLAGS, *PDXGK_POINTERFLAGS;


/* =========================================================================
 * DXGK_PRESENTCAPS  -  hardware present/flip capabilities
 * =========================================================================
 */
typedef struct _DXGK_PRESENTCAPS
{
    union
    {
        struct
        {
            UINT    NoScreenToScreenBlt             : 1;
            UINT    NoOverlapScreenBlt              : 1;
            UINT    SupportKernelModeCommandBuffer  : 1;
            UINT    NoSameBitmapAlphaBlend          : 1;
            UINT    NoSameBitmapStretchBlt          : 1;
            UINT    NoSameBitmapTransparentBlt      : 1;
            UINT    DriverSupportedPointerCursors   : 2;
            UINT    MaxQueuedFlipOnVSync             : 3;
            UINT    FlipOnVSyncWithNoWait            : 1;
            UINT    Reserved                        : 20;
        };
        UINT    Value;
    };
} DXGK_PRESENTCAPS, *PDXGK_PRESENTCAPS;

typedef union _DXGK_FLIPCAPS
{
    struct
    {
        UINT FlipOnVSyncMmIo : 1;
        UINT Reserved        : 31;
    };
    UINT Value;
} DXGK_FLIPCAPS, *PDXGK_FLIPCAPS;

typedef union _DXGK_SCHEDULINGCAPS
{
    struct
    {
        UINT MultiEngineAware : 1;
        UINT PreemptionAware  : 1;
        UINT Reserved         : 30;
    };
    UINT Value;
} DXGK_SCHEDULINGCAPS, *PDXGK_SCHEDULINGCAPS;

typedef union _DXGK_MEMORYMANAGEMENTCAPS
{
    struct
    {
        UINT SectionBackedPrimary : 1;
        UINT Reserved             : 31;
    };
    UINT Value;
} DXGK_MEMORYMANAGEMENTCAPS, *PDXGK_MEMORYMANAGEMENTCAPS;

#define DXGK_MAX_ASYMETRICAL_PROCESSING_NODES 64

typedef struct _DXGK_GPUENGINETOPOLOGY
{
    UINT NbAsymetricProcessingNodes;
    UINT Reserved[DXGK_MAX_ASYMETRICAL_PROCESSING_NODES];
} DXGK_GPUENGINETOPOLOGY, *PDXGK_GPUENGINETOPOLOGY;


/* =========================================================================
 * DXGK_DRIVERCAPS (+ the deprecated _ADVSCH_ sub-structures it embeds)
 *
 * Capability record returned by
 * DxgkDdiQueryAdapterInfo(DXGKQAITYPE_DRIVERCAPS).
 *
 * Layout matches the genuine WDK d3dkmddi.h (winsdk-10 10.0.16299) exactly
 * through the WDDM 2.1 fields, so the structure is ABI-compatible with
 * miniports built against the real WDK (viogpudo): dxgkrnl reads pointer
 * caps and support flags from foreign drivers.
 * =========================================================================
 */
typedef struct _DXGK_VIRTUALADDRESSCAPS_DEPRECATED
{
    union
    {
        struct
        {
            UINT PrivilegedMemorySupported  : 1;
            UINT ReadOnlyMemorySupported    : 1;
            UINT Reserved                   : 30;
        };
        UINT        Value;
    };
    UINT VirtualAddressBitCount;
    UINT PageTableCoverageBitCount;
    UINT PageDirectoryEntrySize;
    UINT PageDirectorySegment;
    UINT PageTableSegment;
    UINT IdealGPUPageSize;
} DXGK_VIRTUALADDRESSCAPS_DEPRECATED;

typedef struct _DXGK_DMABUFFERCAPS_DEPRECATED
{
    struct
    {
        UINT Size;
        UINT PrivateDriverDataSize;
        UINT SegmentId;
        UINT Reserved1;
        UINT Reserved[16];
    } PresentDmaBuffer;

    struct
    {
        UINT Size;
        UINT PrivateDriverDataSize;
        UINT SegmentId;
        UINT Reserved1;
        UINT Reserved[16];
    } PagingDmaBuffer;
} DXGK_DMABUFFERCAPS_DEPRECATED;

typedef enum _DXGK_WDDMVERSION
{
     DXGKDDI_WDDMv1_ENUM   = 0x1000,
     DXGKDDI_WDDMv1_2_ENUM = 0x1200,
     DXGKDDI_WDDMv1_3_ENUM = 0x1300,
     DXGKDDI_WDDMv2_ENUM   = 0x2000,
     DXGKDDI_WDDMv2_1_ENUM = 0x2100,
     DXGKDDI_WDDMv2_2_ENUM = 0x2200,
     DXGKDDI_WDDMv2_3_ENUM = 0x2300,
} DXGK_WDDMVERSION;

typedef struct _DXGK_DRIVERCAPS
{
    PHYSICAL_ADDRESS        HighestAcceptableAddress;
    UINT                    MaxAllocationListSlotId;
    SIZE_T                  ApertureSegmentCommitLimit;
    UINT                    MaxPointerWidth;
    UINT                    MaxPointerHeight;
    DXGK_POINTERFLAGS       PointerCaps;
    UINT                    InterruptMessageNumber;
    UINT                    NumberOfSwizzlingRanges;
    UINT                    MaxOverlays;
    UINT                    GammaRampCaps;
    DXGK_PRESENTCAPS        PresentationCaps;
    UINT                    MaxQueuedFlipOnVSync;
    DXGK_FLIPCAPS           FlipCaps;
    DXGK_SCHEDULINGCAPS     SchedulingCaps;
    DXGK_MEMORYMANAGEMENTCAPS MemoryManagementCaps;
    DXGK_GPUENGINETOPOLOGY  GpuEngineTopology;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    DXGK_WDDMVERSION        WDDMVersion;
    DXGK_VIRTUALADDRESSCAPS_DEPRECATED Reserved;
    DXGK_DMABUFFERCAPS_DEPRECATED      Reserved1;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    D3DKMDT_PREEMPTION_CAPS PreemptionCaps;
    BOOLEAN                 SupportNonVGA;
    BOOLEAN                 SupportSmoothRotation;
    BOOLEAN                 SupportPerEngineTDR;
    BOOLEAN                 SupportDirectFlip;
    BOOLEAN                 SupportMultiPlaneOverlay;
    BOOLEAN                 SupportRuntimePowerManagement;
    BOOLEAN                 SupportSurpriseRemovalInHibernation;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    BOOLEAN                 HybridDiscrete;
    UINT                    MaxOverlayPlanes;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    BOOLEAN                 HybridIntegrated;
    D3DGPU_VIRTUAL_ADDRESS  InternalGpuVirtualAddressRangeStart;
    D3DGPU_VIRTUAL_ADDRESS  InternalGpuVirtualAddressRangeEnd;
    BOOLEAN                 SupportSurpriseRemoval;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    BOOLEAN                 SupportMultiPlaneOverlayImmediateFlip;
    BOOLEAN                 CursorScaledWithMultiPlaneOverlayPlane0;
    BOOLEAN                 HybridAcpiChainingRequired;
    UINT                    MaxQueuedMultiPlaneOverlayFlipVSync;
#endif
} DXGK_DRIVERCAPS, *PDXGK_DRIVERCAPS;


/* =========================================================================
 * DXGK_SEGMENTFLAGS  -  per-segment capability bits
 * =========================================================================
 */
typedef struct _DXGK_SEGMENTFLAGS
{
    union
    {
        struct
        {
            UINT    Aperture                            : 1;
            UINT    Agp                                 : 1;
            UINT    CpuVisible                          : 1;
            UINT    UseBanking                          : 1;
            UINT    CacheCoherent                       : 1;
            UINT    PitchAlignment                      : 1;
            UINT    PopulatedFromSystemMemory           : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
            UINT    PreservedDuringStandby              : 1;
            UINT    PreservedDuringHibernate            : 1;
            UINT    PartiallyPreservedDuringHibernate   : 1;
            UINT    DirectFlip                          : 1;
            UINT    Reserved                            : 21;
#else
            UINT    Reserved                            : 25;
#endif
        };
        UINT    Value;
    };
} DXGK_SEGMENTFLAGS, *PDXGK_SEGMENTFLAGS;


/* =========================================================================
 * DXGK_SEGMENTDESCRIPTOR
 *
 * Describes one GPU memory segment.  Miniport fills an array of these in
 * DxgkDdiQueryAdapterInfo(DXGKQAITYPE_QUERYSEGMENT).
 * =========================================================================
 */
typedef struct _DXGK_SEGMENTDESCRIPTOR
{
    PHYSICAL_ADDRESS    BaseAddress;            /* segment base (physical)      */
    PHYSICAL_ADDRESS    CpuTranslatedAddress;   /* CPU-accessible base, if any  */
    SIZE_T              Size;                   /* segment size in bytes        */
    UINT                NbOfBanks;
    SIZE_T             *pBankRangeTable;
    SIZE_T              CommitLimit;            /* max committed (aperture segs) */
    DXGK_SEGMENTFLAGS   Flags;
} DXGK_SEGMENTDESCRIPTOR, *PDXGK_SEGMENTDESCRIPTOR;

/*
 * DXGK_SEGMENTDESCRIPTOR3 / DXGK_QUERYSEGMENTOUT / DXGK_QUERYSEGMENTOUT3
 *
 * Layouts match the genuine WDK d3dkmddi.h (winsdk-10 10.0.16299) exactly.
 * Note DESCRIPTOR3 is NOT a field-reordered DESCRIPTOR: Flags moves to the
 * front and two SIZE_T fields are appended.  QUERYSEGMENT[3] is a two-pass
 * protocol: first call has pSegmentDescriptor == NULL and the miniport sets
 * NbSegment; the second call provides the descriptor array to fill.
 */
typedef struct _DXGK_SEGMENTDESCRIPTOR3
{
    DXGK_SEGMENTFLAGS       Flags;
    PHYSICAL_ADDRESS        BaseAddress;
    PHYSICAL_ADDRESS        CpuTranslatedAddress;
    SIZE_T                  Size;
    UINT                    NbOfBanks;
    SIZE_T                 *pBankRangeTable;
    SIZE_T                  CommitLimit;
    SIZE_T                  SystemMemoryEndAddress;
    SIZE_T                  Reserved;
} DXGK_SEGMENTDESCRIPTOR3, *PDXGK_SEGMENTDESCRIPTOR3;

typedef struct _DXGK_QUERYSEGMENTOUT
{
    UINT                        NbSegment;
    DXGK_SEGMENTDESCRIPTOR     *pSegmentDescriptor;
    UINT                        PagingBufferSegmentId;
    UINT                        PagingBufferSize;
    UINT                        PagingBufferPrivateDataSize;
} DXGK_QUERYSEGMENTOUT, *PDXGK_QUERYSEGMENTOUT;

typedef struct _DXGK_QUERYSEGMENTOUT3
{
    UINT                        NbSegment;
    DXGK_SEGMENTDESCRIPTOR3    *pSegmentDescriptor;
    UINT                        PagingBufferSegmentId;
    UINT                        PagingBufferSize;
    UINT                        PagingBufferPrivateDataSize;
} DXGK_QUERYSEGMENTOUT3, *PDXGK_QUERYSEGMENTOUT3;

/*
 * QUERYSEGMENT4 flavour (WDDM 2.0+), layouts verbatim from the genuine WDK.
 * Same two-pass protocol, but descriptors return through a BYTE array whose
 * element stride the miniport reports in SegmentDescriptorStride.
 */
typedef struct _DXGK_CPUHOSTAPERTURE
{
    UINT64  PhysicalAddress;
    UINT32  SizeInPages;
} DXGK_CPUHOSTAPERTURE;

typedef struct _DXGK_QUERYSEGMENTIN4
{
    UINT                    PhysicalAdapterIndex;
} DXGK_QUERYSEGMENTIN4;

typedef struct _DXGK_SEGMENTDESCRIPTOR4
{
    DXGK_SEGMENTFLAGS        Flags;
    PHYSICAL_ADDRESS         BaseAddress;
    SIZE_T                   Size;
    SIZE_T                   CommitLimit;
    SIZE_T                   SystemMemoryEndAddress;
    union
    {
        PHYSICAL_ADDRESS     CpuTranslatedAddress;
        DXGK_CPUHOSTAPERTURE CpuHostAperture;
    };
    UINT                     NumInvalidMemoryRanges;
    SIZE_T                   VprRangeStartOffset;
    SIZE_T                   VprRangeSize;
    UINT                     VprAlignment;
    UINT                     NumVprSupported;
    UINT                     VprReserveSize;
} DXGK_SEGMENTDESCRIPTOR4, *PDXGK_SEGMENTDESCRIPTOR4;

typedef struct _DXGK_QUERYSEGMENTOUT4
{
    UINT    NbSegment;
    BYTE   *pSegmentDescriptor;
    UINT    PagingBufferSegmentId;
    UINT    PagingBufferSize;
    UINT    PagingBufferPrivateDataSize;
    SIZE_T  SegmentDescriptorStride;
} DXGK_QUERYSEGMENTOUT4, *PDXGK_QUERYSEGMENTOUT4;


/* =========================================================================
 * DXGK_NODETYPE  /  DXGK_NODEMETADATA
 *
 * Returned by DxgkDdiQueryAdapterInfo(DXGKQAITYPE_NODEMETADATA).
 * =========================================================================
 */
typedef enum _DXGK_NODETYPE
{
    DXGK_NODE_TYPE_NONE              = 0,
    DXGK_NODE_TYPE_3D                = 1,
    DXGK_NODE_TYPE_VIDEO_DECODE      = 2,
    DXGK_NODE_TYPE_VIDEO_ENCODE      = 3,
    DXGK_NODE_TYPE_VIDEO_PROCESSING  = 4,
    DXGK_NODE_TYPE_DISPLAY           = 5,
    DXGK_NODE_TYPE_SCAN_OUT          = 6,
    DXGK_NODE_TYPE_COPY              = 7,
    DXGK_NODE_TYPE_OVERLAY           = 8,
    DXGK_NODE_TYPE_CRYPTO            = 9,
    DXGK_NODE_TYPE_MAX,
} DXGK_NODETYPE;

/* DXGK_NODEMETADATA is also defined in d3dkmdt.h; guard against redefinition. */
#ifndef _DXGK_NODEMETADATA_DEFINED
#define _DXGK_NODEMETADATA_DEFINED
typedef union _DXGK_NODEMETADATA_FLAGS
{
    struct
    {
        UINT ContextSchedulingSupported : 1;
        UINT Reserved                   : 31;
    };
    UINT Value;
} DXGK_NODEMETADATA_FLAGS, *PDXGK_NODEMETADATA_FLAGS;

typedef struct _DXGK_NODEMETADATA
{
    DXGK_NODETYPE   EngineType;
    WCHAR           FriendlyName[64];
    DXGK_NODEMETADATA_FLAGS Flags;
    BOOLEAN         GpuMmuSupported;
    BOOLEAN         IoMmuSupported;
} DXGK_NODEMETADATA, *PDXGK_NODEMETADATA;

typedef DXGK_NODEMETADATA DXGKARG_GETNODEMETADATA;
typedef DXGKARG_GETNODEMETADATA *PDXGKARG_GETNODEMETADATA;
#endif


/* =========================================================================
 * DXGKARG_CREATEDEVICE
 * =========================================================================
 */
typedef struct _DXGK_DEVICEINFOFLAGS
{
    union
    {
        struct
        {
            UINT GuaranteedDmaBufferContract : 1;
            UINT Reserved                    : 31;
        };
        UINT Value;
    };
} DXGK_DEVICEINFOFLAGS, *PDXGK_DEVICEINFOFLAGS;

typedef struct _DXGK_DEVICEINFO
{
    UINT                 DmaBufferSize;
    UINT                 DmaBufferSegmentSet;
    UINT                 DmaBufferPrivateDataSize;
    UINT                 AllocationListSize;
    UINT                 PatchLocationListSize;
    DXGK_DEVICEINFOFLAGS Flags;
} DXGK_DEVICEINFO, *PDXGK_DEVICEINFO;

typedef struct _DXGK_CREATEDEVICEFLAGS
{
    union
    {
        struct
        {
            UINT SystemDevice          : 1;
            UINT GdiDevice             : 1;
            UINT Reserved              : 29;
            UINT DXGK_DEVICE_RESERVED0 : 1;
        };
        UINT    Value;
    };
} DXGK_CREATEDEVICEFLAGS;

typedef struct _DXGKARG_CREATEDEVICE
{
    HANDLE hDevice;
    union
    {
        DXGK_CREATEDEVICEFLAGS Flags;
        DXGK_DEVICEINFO       *pInfo;
    };
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    ULONG  Pasid;
    HANDLE hKmdProcess;
#endif
} DXGKARG_CREATEDEVICE, *PDXGKARG_CREATEDEVICE;


/* =========================================================================
 * DXGKARG_CREATEALLOCATION / DXGKARG_DESTROYALLOCATION
 * =========================================================================
 */
typedef union _DXGK_CREATEALLOCATIONFLAGS
{
    struct
    {
        UINT Resource     : 1;
        UINT Shared       : 1;
        UINT Reserved     : 30;
    };
    UINT Value;
} DXGK_CREATEALLOCATIONFLAGS, *PDXGK_CREATEALLOCATIONFLAGS;

typedef struct _DXGKARG_CREATEALLOCATION
{
    PVOID               pPrivateDriverData;
    UINT                PrivateDriverDataSize;
    UINT                NumAllocations;
    DXGK_ALLOCATIONINFO *pAllocationInfo;
    HANDLE              hResource;
    DXGK_CREATEALLOCATIONFLAGS Flags;
} DXGKARG_CREATEALLOCATION, *PDXGKARG_CREATEALLOCATION;

typedef union _DXGK_DESTROYALLOCATIONFLAGS
{
    struct
    {
        UINT DestroyResource  : 1;
        UINT Reserved         : 31;
    };
    UINT Value;
} DXGK_DESTROYALLOCATIONFLAGS, *PDXGK_DESTROYALLOCATIONFLAGS;

typedef struct _DXGKARG_DESTROYALLOCATION
{
    UINT            NumAllocations;
    union
    {
        CONST HANDLE *phAllocation;
        CONST HANDLE *pAllocationList;
    };
    HANDLE          hResource;
    DXGK_DESTROYALLOCATIONFLAGS Flags;
} DXGKARG_DESTROYALLOCATION, *PDXGKARG_DESTROYALLOCATION;


/* =========================================================================
 * DXGKARG_DESCRIBEALLOCATION
 * =========================================================================
 */
typedef struct _DXGKARG_DESCRIBEALLOCATION
{
    HANDLE                      hAllocation;
    UINT                        Width;
    UINT                        Height;
    D3DDDIFORMAT                Format;
    D3DDDI_MULTISAMPLINGMETHOD  MultisampleMethod;
    D3DDDI_RATIONAL             RefreshRate;
    union
    {
        UINT                    PrivateFormatAttribute;
        UINT                    PrivateDriverFormatAttribute;
    };
} DXGKARG_DESCRIBEALLOCATION, *PDXGKARG_DESCRIBEALLOCATION;


/* =========================================================================
 * DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA
 * =========================================================================
 */
typedef D3DKMDT_STANDARDALLOCATION_TYPE DXGK_STANDARDALLOCATIONTYPE;
#define DXGK_STDALLOCATION_SHAREDPRIMARYSURFACE D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE
#define DXGK_STDALLOCATION_SHADOWSURFACE D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE
#define DXGK_STDALLOCATION_STAGINGSURFACE D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE
#define DXGK_STDALLOCATION_GDISURFACE D3DKMDT_STANDARDALLOCATION_GDISURFACE

typedef struct _DXGK_SHAREDPRIMARYSURFACEDATA
{
    UINT                            Width;
    UINT                            Height;
    D3DDDIFORMAT                    Format;
    D3DDDI_RATIONAL                 RefreshRate;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
} DXGK_SHAREDPRIMARYSURFACEDATA, *PDXGK_SHAREDPRIMARYSURFACEDATA;

typedef struct _DXGK_SHADOWSURFACEDATA
{
    UINT            Width;
    UINT            Height;
    D3DDDIFORMAT    Format;
    UINT            Pitch;                          /* out */
    UINT            PrivateDriverFormatAttribute;   /* out */
} DXGK_SHADOWSURFACEDATA, *PDXGK_SHADOWSURFACEDATA;

typedef struct _DXGK_STAGINGSURFACEDATA
{
    UINT    Width;
    UINT    Height;
    UINT    Pitch;  /* out */
} DXGK_STAGINGSURFACEDATA, *PDXGK_STAGINGSURFACEDATA;

typedef struct _DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA
{
    D3DKMDT_STANDARDALLOCATION_TYPE StandardAllocationType;
    union
    {
        D3DKMDT_SHAREDPRIMARYSURFACEDATA *pCreateSharedPrimarySurfaceData;
        D3DKMDT_SHADOWSURFACEDATA       *pCreateShadowSurfaceData;
        D3DKMDT_STAGINGSURFACEDATA      *pCreateStagingSurfaceData;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
        D3DKMDT_GDISURFACEDATA           *pCreateGdiSurfaceData;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        D3DKMDT_VIRTUALGPUSURFACEDATA    *pCreateVirtualGpuSurfaceData;
#endif
    };
    PVOID                           pAllocationPrivateDriverData;
    UINT                            AllocationPrivateDriverDataSize;
    PVOID                           pResourcePrivateDriverData;
    UINT                            ResourcePrivateDriverDataSize;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    UINT                            PhysicalAdapterIndex;
#endif
} DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, *PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA;


/* =========================================================================
 * DXGKARG_OPENALLOCATION / DXGKARG_CLOSEALLOCATION
 * =========================================================================
 */
typedef struct _DXGK_OPENALLOCATIONINFO
{
    D3DKMT_HANDLE hAllocation;
    VOID       *pPrivateDriverData;
    UINT        PrivateDriverDataSize;
    HANDLE      hDeviceSpecificAllocation;  /* out */
} DXGK_OPENALLOCATIONINFO, *PDXGK_OPENALLOCATIONINFO;

typedef struct _DXGK_OPENALLOCATIONFLAGS
{
    union
    {
        struct
        {
            UINT Create   : 1;
            UINT ReadOnly : 1;
            UINT Reserved : 30;
        };
        UINT Value;
    };
} DXGK_OPENALLOCATIONFLAGS, *PDXGK_OPENALLOCATIONFLAGS;

typedef struct _DXGKARG_OPENALLOCATION
{
    UINT                        NumAllocations;
    DXGK_OPENALLOCATIONINFO    *pOpenAllocation;
    VOID                       *pPrivateDriverData;
    UINT                        PrivateDriverSize;
    DXGK_OPENALLOCATIONFLAGS    Flags;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    UINT                        SubresourceIndex;
    SIZE_T                      SubresourceOffset;
    UINT                        Pitch;
#endif
} DXGKARG_OPENALLOCATION, *PDXGKARG_OPENALLOCATION;

typedef struct _DXGKARG_CLOSEALLOCATION
{
    UINT            NumAllocations;
    union
    {
        CONST HANDLE *phAllocation;
        CONST HANDLE *pOpenHandleList;
    };
} DXGKARG_CLOSEALLOCATION, *PDXGKARG_CLOSEALLOCATION;

typedef enum _DXGK_HANDLE_TYPE
{
    DXGK_HANDLE_ALLOCATION = 1,
    DXGK_HANDLE_RESOURCE   = 2
} DXGK_HANDLE_TYPE;

typedef struct _DXGKCB_GETHANDLEDATAFLAGS
{
    union
    {
        struct
        {
            UINT DeviceSpecific : 1;
            UINT Reserved       : 31;
        };
        UINT Value;
    };
} DXGKCB_GETHANDLEDATAFLAGS;

typedef struct _DXGKARGCB_GETHANDLEDATA
{
    D3DKMT_HANDLE             hObject;
    DXGK_HANDLE_TYPE          Type;
    DXGKCB_GETHANDLEDATAFLAGS Flags;
} DXGKARGCB_GETHANDLEDATA;

typedef PVOID
(APIENTRY *DXGKCB_GETHANDLEDATA)(
    _In_ CONST DXGKARGCB_GETHANDLEDATA *HandleData);

typedef PVOID DXGKARG_RELEASE_HANDLE;
typedef DXGKARG_RELEASE_HANDLE *PDXGKARG_RELEASE_HANDLE;

typedef PVOID
(APIENTRY *DXGKCB_ACQUIREHANDLEDATA)(
    _In_ CONST DXGKARGCB_GETHANDLEDATA *HandleData,
    _Out_ PDXGKARG_RELEASE_HANDLE ReleaseHandle);

typedef struct _DXGKARGCB_RELEASEHANDLEDATA
{
    DXGKARG_RELEASE_HANDLE ReleaseHandle;
    DXGK_HANDLE_TYPE Type;
} DXGKARGCB_RELEASEHANDLEDATA;

typedef VOID
(APIENTRY *DXGKCB_RELEASEHANDLEDATA)(
    _In_ CONST DXGKARGCB_RELEASEHANDLEDATA HandleData);


/* =========================================================================
 * DXGKARG_PATCH
 * =========================================================================
 */
/* Layout matches the genuine WDK d3dkmddi.h (winsdk-10 10.0.16299). */
typedef struct _DXGK_PATCHFLAGS
{
    union
    {
        struct
        {
            UINT                    Paging              : 1;
            UINT                    Present             : 1;
            UINT                    RedirectedPresent   : 1;
            UINT                    NullRendering       : 1;
            UINT                    Reserved            :28;
        };
        UINT                        Value;
    };
} DXGK_PATCHFLAGS;

typedef struct _DXGKARG_PATCH
{
    union
    {
        HANDLE                          hDevice;    /* non-MultiEngineAware */
        HANDLE                          hContext;   /* MultiEngineAware     */
    };
    UINT                            DmaBufferSegmentId;
    PHYSICAL_ADDRESS                DmaBufferPhysicalAddress;
    PVOID                           pDmaBuffer;
    UINT                            DmaBufferSize;
    UINT                            DmaBufferSubmissionStartOffset;
    UINT                            DmaBufferSubmissionEndOffset;
    PVOID                           pDmaBufferPrivateData;
    UINT                            DmaBufferPrivateDataSize;
    UINT                            DmaBufferPrivateDataSubmissionStartOffset;
    UINT                            DmaBufferPrivateDataSubmissionEndOffset;
    CONST struct _DXGK_ALLOCATIONLIST *pAllocationList; /* DXGK_ALLOCATIONLIST,
                                                            defined below */
    UINT                            AllocationListSize;
    CONST D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT                            PatchLocationListSize;
    UINT                            PatchLocationListSubmissionStart;
    UINT                            PatchLocationListSubmissionLength;
    UINT                            SubmissionFenceId;
    DXGK_PATCHFLAGS                 Flags;
    UINT                            EngineOrdinal;
} DXGKARG_PATCH, *PDXGKARG_PATCH;


/* =========================================================================
 * DXGKARG_SUBMITCOMMAND
 * =========================================================================
 */
typedef struct _DXGK_SUBMITCOMMANDFLAGS
{
    union
    {
        struct
        {
            UINT Paging               : 1;
            UINT Present              : 1;
            UINT RedirectedPresent    : 1;
            UINT NullRendering        : 1;
            UINT Flip                 : 1;
            UINT FlipWithNoWait       : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
            UINT ContextSwitch        : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
            UINT Resubmission         : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
            UINT VirtualMachineData   : 1;
            UINT Reserved             : 23;
#else
            UINT Reserved             : 24;
#endif
#else
            UINT Reserved             : 25;
#endif
#else
            UINT Reserved             : 26;
#endif
        };
        UINT Value;
    };
} DXGK_SUBMITCOMMANDFLAGS, *PDXGK_SUBMITCOMMANDFLAGS;

typedef struct _DXGKARG_SUBMITCOMMAND
{
    union
    {
        HANDLE hDevice;
        HANDLE hContext;
    };
    UINT                         DmaBufferSegmentId;
    PHYSICAL_ADDRESS             DmaBufferPhysicalAddress;
    UINT                         DmaBufferSize;
    UINT                         DmaBufferSubmissionStartOffset;
    UINT                         DmaBufferSubmissionEndOffset;
    PVOID                        pDmaBufferPrivateData;
    UINT                         DmaBufferPrivateDataSize;
    UINT                         DmaBufferPrivateDataSubmissionStartOffset;
    UINT                         DmaBufferPrivateDataSubmissionEndOffset;
    UINT                         SubmissionFenceId;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DDDI_FLIPINTERVAL_TYPE     FlipInterval;
    DXGK_SUBMITCOMMANDFLAGS      Flags;
    UINT                         EngineOrdinal;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    D3DGPU_VIRTUAL_ADDRESS       DmaBufferVirtualAddress;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    UINT                         NodeOrdinal;
#endif
} DXGKARG_SUBMITCOMMAND, *PDXGKARG_SUBMITCOMMAND;


/* =========================================================================
 * DXGKARG_PREEMPTCOMMAND
 * =========================================================================
 */
typedef struct _DXGKARG_PREEMPTCOMMAND
{
    UINT    PreemptionFenceId;
    UINT    NodeOrdinal;
    UINT    EngineOrdinal;
    UINT    Flags;
} DXGKARG_PREEMPTCOMMAND, *PDXGKARG_PREEMPTCOMMAND;

typedef struct _DXGKARG_CANCELCOMMAND
{
    UINT    SubmissionFenceId;
    UINT    NodeOrdinal;
    UINT    EngineOrdinal;
} DXGKARG_CANCELCOMMAND, *PDXGKARG_CANCELCOMMAND;


/* =========================================================================
 * DXGKARG_BUILDPAGINGBUFFER
 * =========================================================================
 */
typedef enum _DXGK_BUILDPAGINGBUFFER_OPERATION
{
    DXGK_OPERATION_TRANSFER                 = 0,
    DXGK_OPERATION_FILL                     = 1,
    DXGK_OPERATION_DISCARD_CONTENT         = 2,
    DXGK_OPERATION_READ_PHYSICAL            = 3,
    DXGK_OPERATION_WRITE_PHYSICAL           = 4,
    DXGK_OPERATION_MAP_APERTURE_SEGMENT     = 5,
    DXGK_OPERATION_UNMAP_APERTURE_SEGMENT   = 6,
    DXGK_OPERATION_SPECIAL_LOCK_TRANSFER    = 7,
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    DXGK_OPERATION_VIRTUAL_TRANSFER         = 8,
    DXGK_OPERATION_VIRTUAL_FILL             = 9,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    DXGK_OPERATION_INIT_CONTEXT_RESOURCE    = 10,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    DXGK_OPERATION_UPDATE_PAGE_TABLE        = 11,
    DXGK_OPERATION_FLUSH_TLB                = 12,
    DXGK_OPERATION_UPDATE_CONTEXT_ALLOCATION = 13,
    DXGK_OPERATION_COPY_PAGE_TABLE_ENTRIES  = 14,
    DXGK_OPERATION_NOTIFY_RESIDENCY         = 15,
#endif
} DXGK_BUILDPAGINGBUFFER_OPERATION;

#define DXGK_OPERATION_MAP_APERTURE DXGK_OPERATION_MAP_APERTURE_SEGMENT
#define DXGK_OPERATION_UNMAP_APERTURE DXGK_OPERATION_UNMAP_APERTURE_SEGMENT

typedef struct _DXGK_TRANSFERFLAGS
{
    union
    {
        struct
        {
            UINT Swizzle          : 1;
            UINT Unswizzle        : 1;
            UINT AllocationIsIdle : 1;
            UINT TransferStart    : 1;
            UINT TransferEnd      : 1;
            UINT Reserved         : 27;
        };
        UINT Value;
    };
} DXGK_TRANSFERFLAGS, *PDXGK_TRANSFERFLAGS;

typedef struct _DXGK_DISCARDCONTENTFLAGS
{
    union
    {
        struct
        {
            UINT AllocationIsIdle : 1;
            UINT Reserved         : 31;
        };
        UINT Value;
    };
} DXGK_DISCARDCONTENTFLAGS, *PDXGK_DISCARDCONTENTFLAGS;

typedef struct _DXGK_MAPAPERTUREFLAGS
{
    union
    {
        struct
        {
            UINT CacheCoherent : 1;
            UINT Reserved      : 31;
        };
        UINT Value;
    };
} DXGK_MAPAPERTUREFLAGS, *PDXGK_MAPAPERTUREFLAGS;

typedef struct _DXGK_BUILDPAGINGBUFFER_TRANSFER
{
    HANDLE          hAllocation;
    UINT            TransferOffset;
    SIZE_T          TransferSize;
    struct
    {
        UINT            SegmentId;
        union
        {
            LARGE_INTEGER   SegmentAddress;
            MDL            *pMdl;
        };
    } Source;
    struct
    {
        UINT            SegmentId;
        union
        {
            LARGE_INTEGER   SegmentAddress;
            MDL            *pMdl;
        };
    } Destination;
    DXGK_TRANSFERFLAGS Flags;
    UINT               MdlOffset;
} DXGK_BUILDPAGINGBUFFER_TRANSFER;

typedef struct _DXGK_BUILDPAGINGBUFFER_FILL
{
    HANDLE          hAllocation;
    SIZE_T          FillSize;
    UINT            FillPattern;
    struct
    {
        UINT          SegmentId;
        LARGE_INTEGER SegmentAddress;
    } Destination;
} DXGK_BUILDPAGINGBUFFER_FILL;

typedef struct _DXGK_BUILDPAGINGBUFFER_DISCARD_CONTENT
{
    HANDLE          hAllocation;
    DXGK_DISCARDCONTENTFLAGS Flags;
    UINT            SegmentId;
    PHYSICAL_ADDRESS SegmentAddress;
} DXGK_BUILDPAGINGBUFFER_DISCARD_CONTENT;

typedef struct _DXGK_BUILDPAGINGBUFFER_MAP_APERTURE
{
    HANDLE          hDevice;
    HANDLE          hAllocation;
    UINT            SegmentId;
    SIZE_T          OffsetInPages;
    SIZE_T          NumberOfPages;
    MDL            *pMdl;
    DXGK_MAPAPERTUREFLAGS Flags;
    ULONG           MdlOffset;
} DXGK_BUILDPAGINGBUFFER_MAP_APERTURE;

typedef struct _DXGK_BUILDPAGINGBUFFER_UNMAP_APERTURE
{
    HANDLE          hDevice;
    HANDLE          hAllocation;
    UINT            SegmentId;
    SIZE_T          OffsetInPages;
    SIZE_T          NumberOfPages;
    PHYSICAL_ADDRESS DummyPage;
} DXGK_BUILDPAGINGBUFFER_UNMAP_APERTURE;

typedef struct _DXGK_BUILDPAGINGBUFFER_SPECIAL_LOCK_TRANSFER
{
    HANDLE hAllocation;
    UINT   TransferOffset;
    SIZE_T TransferSize;
    struct
    {
        UINT SegmentId;
        union
        {
            LARGE_INTEGER SegmentAddress;
            MDL          *pMdl;
        };
    } Source;
    struct
    {
        UINT SegmentId;
        union
        {
            LARGE_INTEGER SegmentAddress;
            MDL          *pMdl;
        };
    } Destination;
    DXGK_TRANSFERFLAGS Flags;
    UINT SwizzlingRangeId;
    UINT SwizzlingRangeData;
} DXGK_BUILDPAGINGBUFFER_SPECIAL_LOCK_TRANSFER;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
typedef struct _DXGK_BUILDPAGINGBUFFER_INIT_CONTEXT_RESOURCE
{
    HANDLE hAllocation;
    struct
    {
        UINT SegmentId;
        union
        {
            LARGE_INTEGER SegmentAddress;
            MDL          *pMdl;
        };
        PVOID VirtualAddress;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress;
#endif
    } Destination;
} DXGK_BUILDPAGINGBUFFER_INIT_CONTEXT_RESOURCE;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
typedef enum _DXGK_PAGETABLEUPDATEMODE
{
    DXGK_PAGETABLEUPDATE_CPU_VIRTUAL,
    DXGK_PAGETABLEUPDATE_GPU_VIRTUAL,
    DXGK_PAGETABLEUPDATE_GPU_PHYSICAL,
} DXGK_PAGETABLEUPDATEMODE;

typedef struct _DXGK_PAGETABLEUPDATEADDRESS
{
    union
    {
        PVOID                   CpuVirtual;
        D3DGPU_PHYSICAL_ADDRESS GpuPhysical;
        D3DGPU_VIRTUAL_ADDRESS  GpuVirtual;
    };
} DXGK_PAGETABLEUPDATEADDRESS;

typedef struct _DXGK_UPDATEPAGETABLEFLAGS
{
    UINT Repeat         : 1;
    UINT InitialUpdate  : 1;
    UINT NotifyEviction : 1;
    UINT Use64KBPages   : 1;
    UINT Reserved       : 28;
} DXGK_UPDATEPAGETABLEFLAGS;

typedef struct _DXGK_BUILDPAGINGBUFFER_COPY_RANGE
{
    UINT                   NumPageTableEntries;
    D3DGPU_VIRTUAL_ADDRESS SrcPageTableAddress;
    D3DGPU_VIRTUAL_ADDRESS DstPageTableAddress;
    UINT                   SrcStartPteIndex;
    UINT                   DstStartPteIndex;
} DXGK_BUILDPAGINGBUFFER_COPY_RANGE;

typedef struct _DXGK_BUILDPAGINGBUFFER_FLUSHTLB
{
    D3DGPU_PHYSICAL_ADDRESS RootPageTableAddress;
    HANDLE                  hProcess;
    D3DGPU_VIRTUAL_ADDRESS  StartVirtualAddress;
    D3DGPU_VIRTUAL_ADDRESS  EndVirtualAddress;
} DXGK_BUILDPAGINGBUFFER_FLUSHTLB;

typedef struct _DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE
{
    UINT                        PageTableLevel;
    HANDLE                      hAllocation;
    DXGK_PAGETABLEUPDATEADDRESS PageTableAddress;
    DXGK_PTE                   *pPageTableEntries;
    UINT                        StartIndex;
    UINT                        NumPageTableEntries;
    UINT                        Reserved0;
    DXGK_UPDATEPAGETABLEFLAGS   Flags;
    UINT64                      DriverProtection;
    UINT64                      AllocationOffsetInBytes;
    HANDLE                      hProcess;
    DXGK_PAGETABLEUPDATEMODE    UpdateMode;
    DXGK_PTE                   *pPageTableEntries64KB;
    D3DGPU_VIRTUAL_ADDRESS      FirstPteVirtualAddress;
} DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE;

typedef struct _DXGK_BUILDPAGINGBUFFER_FILLVIRTUAL
{
    HANDLE                 hAllocation;
    UINT64                 AllocationOffsetInBytes;
    UINT64                 FillSizeInBytes;
    UINT                   FillPattern;
    D3DGPU_VIRTUAL_ADDRESS DestinationVirtualAddress;
} DXGK_BUILDPAGINGBUFFER_FILLVIRTUAL;

typedef enum _DXGK_MEMORY_TRANSFER_DIRECTION
{
    DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM = 0,
    DXGK_MEMORY_TRANSFER_SYSTEM_TO_LOCAL = 1,
    DXGK_MEMORY_TRANSFER_LOCAL_TO_LOCAL  = 2,
} DXGK_MEMORY_TRANSFER_DIRECTION;

typedef struct _DXGK_TRANSFERVIRTUALFLAGS
{
    union
    {
        struct
        {
            UINT Src64KBPages : 1;
            UINT Dst64KBPages : 1;
            UINT Reserved     : 30;
        };
        UINT Flags;
    };
} DXGK_TRANSFERVIRTUALFLAGS;

typedef struct _DXGK_BUILDPAGINGBUFFER_TRANSFERVIRTUAL
{
    HANDLE                         hAllocation;
    UINT64                         AllocationOffsetInBytes;
    UINT64                         TransferSizeInBytes;
    D3DGPU_VIRTUAL_ADDRESS         SourceVirtualAddress;
    D3DGPU_VIRTUAL_ADDRESS         DestinationVirtualAddress;
    D3DGPU_VIRTUAL_ADDRESS         SourcePageTable;
    DXGK_MEMORY_TRANSFER_DIRECTION TransferDirection;
    DXGK_TRANSFERVIRTUALFLAGS      Flags;
    D3DGPU_VIRTUAL_ADDRESS         DestinationPageTable;
} DXGK_BUILDPAGINGBUFFER_TRANSFERVIRTUAL;

typedef struct _DXGK_BUILDPAGINGBUFFER_NOTIFYRESIDENCY
{
    HANDLE                  hAllocation;
    D3DGPU_PHYSICAL_ADDRESS PhysicalAddress;
    union
    {
        UINT Resident : 1;
        UINT Reserved : 31;
    };
} DXGK_BUILDPAGINGBUFFER_NOTIFYRESIDENCY;

typedef struct _DXGK_BUILDPAGINGBUFFER_COPYPAGETABLEENTRIES
{
    UINT                               NumRanges;
    DXGK_BUILDPAGINGBUFFER_COPY_RANGE *pRanges;
} DXGK_BUILDPAGINGBUFFER_COPYPAGETABLEENTRIES;

typedef struct _DXGK_BUILDPAGINGBUFFER_UPDATECONTEXTALLOCATION
{
    D3DGPU_VIRTUAL_ADDRESS ContextAllocation;
    UINT64                 ContextAllocationSize;
    PVOID                  pDriverPrivateData;
    UINT                   DriverPrivateDataSize;
} DXGK_BUILDPAGINGBUFFER_UPDATECONTEXTALLOCATION;
#endif

typedef struct _DXGKARG_BUILDPAGINGBUFFER
{
    PVOID                                   pDmaBuffer;
    UINT                                    DmaSize;
    PVOID                                   pDmaBufferPrivateData;
    UINT                                    DmaBufferPrivateDataSize;
    DXGK_BUILDPAGINGBUFFER_OPERATION        Operation;
    UINT                                    MultipassOffset;
    union
    {
        DXGK_BUILDPAGINGBUFFER_TRANSFER         Transfer;
        DXGK_BUILDPAGINGBUFFER_FILL             Fill;
        DXGK_BUILDPAGINGBUFFER_DISCARD_CONTENT  DiscardContent;
        struct
        {
            UINT             SegmentId;
            PHYSICAL_ADDRESS PhysicalAddress;
        } ReadPhysical;
        struct
        {
            UINT             SegmentId;
            PHYSICAL_ADDRESS PhysicalAddress;
        } WritePhysical;
        DXGK_BUILDPAGINGBUFFER_MAP_APERTURE     MapAperture;
        DXGK_BUILDPAGINGBUFFER_MAP_APERTURE     MapApertureSegment;
        DXGK_BUILDPAGINGBUFFER_UNMAP_APERTURE   UnmapAperture;
        DXGK_BUILDPAGINGBUFFER_UNMAP_APERTURE   UnmapApertureSegment;
        DXGK_BUILDPAGINGBUFFER_SPECIAL_LOCK_TRANSFER SpecialLockTransfer;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
        DXGK_BUILDPAGINGBUFFER_INIT_CONTEXT_RESOURCE InitContextResource;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        DXGK_BUILDPAGINGBUFFER_TRANSFERVIRTUAL TransferVirtual;
        DXGK_BUILDPAGINGBUFFER_FILLVIRTUAL FillVirtual;
        DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE UpdatePageTable;
        DXGK_BUILDPAGINGBUFFER_FLUSHTLB FlushTlb;
        DXGK_BUILDPAGINGBUFFER_COPYPAGETABLEENTRIES CopyPageTableEntries;
        DXGK_BUILDPAGINGBUFFER_UPDATECONTEXTALLOCATION UpdateContextAllocation;
        DXGK_BUILDPAGINGBUFFER_NOTIFYRESIDENCY NotifyResidency;
#endif
        struct
        {
            UINT Reserved[64];
        } Reserved;
    };
    HANDLE hSystemContext;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    D3DGPU_VIRTUAL_ADDRESS DmaBufferGpuVirtualAddress;
    UINT                   DmaBufferWriteOffset;
#endif
} DXGKARG_BUILDPAGINGBUFFER, *PDXGKARG_BUILDPAGINGBUFFER;

typedef struct _DXGK_ALLOCATIONLIST
{
    HANDLE hDeviceSpecificAllocation;
    union
    {
        struct
        {
            UINT WriteOperation : 1;
            UINT SegmentId      : 5;
            UINT Reserved       : 26;
        };
        UINT Value;
    };
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    union
    {
        PHYSICAL_ADDRESS        PhysicalAddress;
        D3DGPU_VIRTUAL_ADDRESS  VirtualAddress;
    };
#else
    PHYSICAL_ADDRESS            PhysicalAddress;
#endif
} DXGK_ALLOCATIONLIST, *PDXGK_ALLOCATIONLIST;
typedef D3DDDI_PATCHLOCATIONLIST DXGK_PATCHLOCATIONLIST, *PDXGK_PATCHLOCATIONLIST;

/* =========================================================================
 * DXGKARG_SETPALETTE
 * =========================================================================
 * Defined in d3dkmdt.h (included via d3dkmdt.h -> this file).
 * We only add the pointer typedef here.
 */
typedef DXGKARG_SETPALETTE *PDXGKARG_SETPALETTE;


/* =========================================================================
 * DXGKARG_SETPOINTERPOSITION / DXGKARG_SETPOINTERSHAPE
 * =========================================================================
 */
typedef struct _DXGKARG_SETPOINTERPOSITION
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    INT     X;
    INT     Y;
    struct {
        UINT    Visible  : 1;
        UINT    Procedural : 1;
        UINT    Reserved : 30;
    } Flags;
} DXGKARG_SETPOINTERPOSITION, *PDXGKARG_SETPOINTERPOSITION;

typedef struct _DXGK_POINTERINFO
{
    union
    {
        struct
        {
            UINT    Monochrome  : 1;
            UINT    Color       : 1;
            UINT    MaskedColor : 1;
            UINT    Reserved    : 29;
        };
        UINT    Value;
    };
} DXGK_POINTERINFO;

typedef struct _DXGKARG_SETPOINTERSHAPE
{
    DXGK_POINTERINFO                Flags;
    UINT                            Width;
    UINT                            Height;
    UINT                            Pitch;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    CONST VOID                     *pPixels;
    UINT                            XHot;
    UINT                            YHot;
} DXGKARG_SETPOINTERSHAPE, *PDXGKARG_SETPOINTERSHAPE;


/* =========================================================================
 * DXGKARG_ESCAPE
 * =========================================================================
 */
typedef struct _DXGKARG_ESCAPE
{
    HANDLE             hDevice;
    D3DDDI_ESCAPEFLAGS Flags;
    PVOID              pPrivateDriverData;
    UINT               PrivateDriverDataSize;
    HANDLE             hContext;
} DXGKARG_ESCAPE, *PDXGKARG_ESCAPE;


/* =========================================================================
 * DXGKARG_COLLECTDBGINFO
 * =========================================================================
 */
/* Layout matches the genuine WDK d3dkmddi.h (winsdk-10 10.0.16299). */
typedef struct _DXGKARG_COLLECTDBGINFO_EXT
{
    UINT BucketingKey;
    UINT CurrentDmaBufferOffset;
    UINT Reserved2;
    UINT Reserved3;
    UINT Reserved4;
    UINT Reserved5;
    UINT Reserved6;
    UINT Reserved7;
} DXGKARG_COLLECTDBGINFO_EXT;

typedef struct _DXGKARG_COLLECTDBGINFO
{
    UINT    Reason;                          /* bugcheck code for the report */
    PVOID   pBuffer;
    SIZE_T  BufferSize;
    DXGKARG_COLLECTDBGINFO_EXT *pExtension;
} DXGKARG_COLLECTDBGINFO, *PDXGKARG_COLLECTDBGINFO;


/* =========================================================================
 * DXGKARG_QUERYCURRENTFENCE
 * =========================================================================
 */
typedef struct _DXGKARG_QUERYCURRENTFENCE
{
    UINT    CurrentFence;   /* out: latest fence completed by GPU */
    UINT    NodeOrdinal;
    UINT    EngineOrdinal;
} DXGKARG_QUERYCURRENTFENCE, *PDXGKARG_QUERYCURRENTFENCE;


/* =========================================================================
 * DXGKARG_ISSUPPORTEDVIDPN
 * =========================================================================
 */
typedef struct _DXGKARG_ISSUPPORTEDVIDPN
{
    D3DKMDT_HVIDPN  hDesiredVidPn;
    BOOLEAN         IsVidPnSupported;   /* out */
} DXGKARG_ISSUPPORTEDVIDPN, *PDXGKARG_ISSUPPORTEDVIDPN;


/* =========================================================================
 * DXGKARG_RECOMMENDFUNCTIONALVIDPN
 * =========================================================================
 */
typedef enum _DXGK_RECOMMENDFUNCTIONALVIDPN_REASON
{
    DXGK_RFVR_UNINITIALIZED = 0,
    DXGK_RFVR_HOTKEY = 1,
    DXGK_RFVR_USERMODE = 2,
    DXGK_RFVR_FIRMWARE = 3,
} DXGK_RECOMMENDFUNCTIONALVIDPN_REASON;

typedef struct _DXGKARG_RECOMMENDFUNCTIONALVIDPN
{
    UINT                                    NumberOfVidPnTargets;
    CONST D3DDDI_VIDEO_PRESENT_TARGET_ID   *pVidPnTargetPrioritizationVector;
    D3DKMDT_HVIDPN                          hRecommendedFunctionalVidPn;
    DXGK_RECOMMENDFUNCTIONALVIDPN_REASON    RequestReason;
    PVOID                                   pPrivateDriverData;
    UINT                                    PrivateDriverDataSize;
} DXGKARG_RECOMMENDFUNCTIONALVIDPN, *PDXGKARG_RECOMMENDFUNCTIONALVIDPN;


/* =========================================================================
 * DXGKARG_ENUMVIDPNCOFUNCMODALITY
 * =========================================================================
 */
/* D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE is defined in d3dkmdt.h */

typedef struct _DXGK_ENUM_PIVOT
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
} DXGK_ENUM_PIVOT;

typedef struct _DXGKARG_ENUMVIDPNCOFUNCMODALITY
{
    D3DKMDT_HVIDPN                                      hConstrainingVidPn;
    D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE               EnumPivotType;
    DXGK_ENUM_PIVOT                                     EnumPivot;
} DXGKARG_ENUMVIDPNCOFUNCMODALITY, *PDXGKARG_ENUMVIDPNCOFUNCMODALITY;


/* =========================================================================
 * DXGKARG_SETVIDPNSOURCEADDRESS
 * =========================================================================
 */
typedef struct _DXGK_SETVIDPNSOURCEADDRESS_FLAGS
{
    union
    {
        struct
        {
            UINT ModeChange               : 1;
            UINT FlipImmediate            : 1;
            UINT FlipOnNextVSync          : 1;
            UINT FlipStereo               : 1;
            UINT FlipStereoTemporaryMono  : 1;
            UINT FlipStereoPreferRight    : 1;
            UINT SharedPrimaryTransition  : 1;
            UINT IndependentFlipExclusive : 1;
            UINT MoveFlip                 : 1;
            UINT Reserved                 : 23;
        };
        UINT Value;
    };
} DXGK_SETVIDPNSOURCEADDRESS_FLAGS, *PDXGK_SETVIDPNSOURCEADDRESS_FLAGS;


#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
typedef struct _DXGK_PRIMARYDATA
{
    HANDLE           hAllocation;
    WORD             SegmentId;
    PHYSICAL_ADDRESS SegmentAddress;
} DXGK_PRIMARYDATA, *PDXGK_PRIMARYDATA;
#endif

typedef struct _DXGKARG_SETVIDPNSOURCEADDRESS
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID   VidPnSourceId;
    UINT                              PrimarySegment;
    PHYSICAL_ADDRESS                  PrimaryAddress;
    HANDLE                            hAllocation;
    UINT                              ContextCount;
    HANDLE                            Context[1 + D3DDDI_MAX_BROADCAST_CONTEXT];
    DXGK_SETVIDPNSOURCEADDRESS_FLAGS Flags;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    UINT                              Duration;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    DXGK_PRIMARYDATA                  PrimaryData[D3DDDI_MAX_BROADCAST_CONTEXT];
    UINT                              DriverPrivateDataSize;
    PVOID                             pDriverPrivateData;
#endif
#endif
} DXGKARG_SETVIDPNSOURCEADDRESS, *PDXGKARG_SETVIDPNSOURCEADDRESS;


/* =========================================================================
 * DXGKARG_SETVIDPNSOURCEVISIBILITY
 * =========================================================================
 */
typedef struct _DXGKARG_SETVIDPNSOURCEVISIBILITY
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    BOOLEAN                         Visible;
} DXGKARG_SETVIDPNSOURCEVISIBILITY, *PDXGKARG_SETVIDPNSOURCEVISIBILITY;


/* =========================================================================
 * DXGKARG_COMMITVIDPN
 * =========================================================================
 */
typedef struct _DXGKARG_COMMITVIDPN_FLAGS
{
    UINT PathPowerTransition    : 1;
    UINT PathPoweredOff         : 1;
    UINT Reserved               : 30;
} DXGKARG_COMMITVIDPN_FLAGS;

typedef struct _DXGKARG_COMMITVIDPN
{
    D3DKMDT_HVIDPN                      hFunctionalVidPn;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID      AffectedVidPnSourceId;
    D3DKMDT_MONITOR_CONNECTIVITY_CHECKS MonitorConnectivityChecks;
    HANDLE                              hPrimaryAllocation;
    DXGKARG_COMMITVIDPN_FLAGS           Flags;
} DXGKARG_COMMITVIDPN, *PDXGKARG_COMMITVIDPN;


/* =========================================================================
 * DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH
 * =========================================================================
 */
typedef struct _DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH
{
    D3DKMDT_VIDPN_PRESENT_PATH  VidPnPresentPathInfo;
} DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH, *PDXGKARG_UPDATEACTIVEVIDPNPRESENTPATH;


/* =========================================================================
 * DXGKARG_RECOMMENDMONITORMODES
 * =========================================================================
 */

/* Forward declaration; full definition in VidPN management headers. */
typedef struct _DXGK_MONITORSOURCEMODESET_INTERFACE
    DXGK_MONITORSOURCEMODESET_INTERFACE;

typedef struct _DXGKARG_RECOMMENDMONITORMODES
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID              VideoPresentTargetId;
    D3DKMDT_HMONITORSOURCEMODESET               hMonitorSourceModeSet;
    CONST DXGK_MONITORSOURCEMODESET_INTERFACE  *pMonitorSourceModeSetInterface;
} DXGKARG_RECOMMENDMONITORMODES, *PDXGKARG_RECOMMENDMONITORMODES;


/* =========================================================================
 * DXGKARG_RECOMMENDVIDPNTOPOLOGY
 * =========================================================================
 */

typedef enum _DXGK_RECOMMENDVIDPNTOPOLOGY_REASON
{
    DXGK_RVT_UNINITIALIZED = 0,
    DXGK_RVT_INITIALIZATION_NOLKG = 1,
    DXGK_RVT_AUGMENTATION_NOLKG = 2,
    DXGK_RVT_AUGMENTATION_LKGOVERRIDE = 3,
    DXGK_RVT_INITIALIZATION_LKGOVERRIDE = 4,
} DXGK_RECOMMENDVIDPNTOPOLOGY_REASON;

typedef struct _DXGKARG_RECOMMENDVIDPNTOPOLOGY
{
    D3DKMDT_HVIDPN                         hVidPn;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID         VidPnSourceId;
    DXGK_RECOMMENDVIDPNTOPOLOGY_REASON     RequestReason;
    D3DKMDT_HVIDPNTOPOLOGY                 hFallbackTopology;
} DXGKARG_RECOMMENDVIDPNTOPOLOGY, *PDXGKARG_RECOMMENDVIDPNTOPOLOGY;


/* =========================================================================
 * DXGKARG_GETSCANLINE
 * =========================================================================
 */
typedef struct _DXGKARG_GETSCANLINE
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    BOOLEAN                         InVerticalBlank;    /* out */
    UINT                            ScanLine;           /* out */
} DXGKARG_GETSCANLINE, *PDXGKARG_GETSCANLINE;


/* =========================================================================
 * DXGKARG_CONTROLINTERRUPT
 * =========================================================================
 */
typedef struct _DXGKARG_CONTROLINTERRUPT
{
    ULONG                           InterruptType;  /* DXGK_INTERRUPT_* constant */
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    BOOLEAN                         EnableInterrupt;
} DXGKARG_CONTROLINTERRUPT, *PDXGKARG_CONTROLINTERRUPT;


/* =========================================================================
 * DXGKARG_CREATECONTEXT
 * =========================================================================
 */
typedef struct _DXGK_CREATECONTEXTFLAGS
{
    union
    {
        struct
        {
            UINT    SystemContext            : 1;
            UINT    GdiContext               : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
            UINT    VirtualAddressing        : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
            UINT    SystemProtectedContext   : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
            UINT    HwQueueSupported         : 1;
            UINT    Reserved                 : 27;
#else
            UINT    Reserved                 : 28;
#endif
#else
            UINT    Reserved                 : 29;
#endif
#else
            UINT    Reserved                 : 30;
#endif
        };
        UINT    Value;
    };
} DXGK_CREATECONTEXTFLAGS;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
typedef struct _DXGK_CONTEXTINFO_CAPS
{
    union
    {
        struct
        {
            UINT NoPatchingRequired     : 1;
            UINT DriverManagesResidency : 1;
            UINT UseIoMmu               : 1;
            UINT Reserved               : 29;
        };
        UINT Value;
    };
} DXGK_CONTEXTINFO_CAPS, *PDXGK_CONTEXTINFO_CAPS;
#endif

/*
 * DXGK_CONTEXTINFO
 *
 * DMA buffer geometry returned by the miniport via DxgkDdiCreateContext.
 * Governs the size of the DMA command buffer and the auxiliary lists that
 * dxgkrnl allocates for the context's execution ring.
 *
 * Matches the Vista WDK layout (WDDM 1.0).
 */
typedef struct _DXGK_CONTEXTINFO
{
    UINT    DmaBufferSize;              /* bytes per DMA command buffer        */
    UINT    DmaBufferSegmentSet;        /* segment bitmask for DMA buffer      */
    UINT    DmaBufferPrivateDataSize;   /* private data appended to DMA buffer */
    UINT    AllocationListSize;         /* entries in the allocation list      */
    UINT    PatchLocationListSize;      /* entries in the patch location list  */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    UINT    Reserved;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    DXGK_CONTEXTINFO_CAPS Caps;
    ULONG                 PagingCompanionNodeId;
#endif
} DXGK_CONTEXTINFO, *PDXGK_CONTEXTINFO;

#ifndef DXGK_ALLOCATION_LIST_SIZE_GDICONTEXT
#define DXGK_ALLOCATION_LIST_SIZE_GDICONTEXT 256
#endif

typedef struct _DXGKARG_CREATECONTEXT
{
    /*
     * hContext (in/out):
     *   IN:  dxgkrnl passes a kernel-mode token identifying the context
     *        (the DXGKRNL_CONTEXT pointer cast to HANDLE).
     *   OUT: the miniport overwrites this with its own opaque per-context
     *        handle, which dxgkrnl stores as hMiniportContext.
     *
     * This matches the Vista WDK (WDDM 1.0) DXGKARG_CREATECONTEXT layout.
     */
    HANDLE                      hContext;

    UINT                        NodeOrdinal;
    UINT                        EngineAffinity;
    DXGK_CREATECONTEXTFLAGS     Flags;
    PVOID                       pPrivateDriverData;
    UINT                        PrivateDriverDataSize;

    /*
     * ContextInfo (out):
     *   The miniport writes DMA buffer geometry here on successful return.
     *   dxgkrnl reads it to set up the submission ring for this context.
     */
    DXGK_CONTEXTINFO            ContextInfo;
} DXGKARG_CREATECONTEXT, *PDXGKARG_CREATECONTEXT;


/* =========================================================================
 * DXGKARG_DESTROYCONTEXT
 * =========================================================================
 */
typedef struct _DXGKARG_DESTROYCONTEXT
{
    HANDLE  hContext;
} DXGKARG_DESTROYCONTEXT, *PDXGKARG_DESTROYCONTEXT;


/* =========================================================================
 * DXGKARG_PRESENT / DXGKARG_RENDER
 * =========================================================================
 */
typedef struct _DXGK_PRESENTFLAGS
{
    union
    {
        struct
        {
            UINT    Blt             : 1;
            UINT    ColorFill       : 1;
            UINT    Flip            : 1;
            UINT    FlipWithNoWait  : 1;
            UINT    SrcColorKey     : 1;
            UINT    DstColorKey     : 1;
            UINT    LinearToSrgb    : 1;
            UINT    Rotate          : 1;
            UINT    Reserved        : 24;
        };
        UINT    Value;
    };
} DXGK_PRESENTFLAGS;

#ifndef DXGK_PRESENT_SOURCE_INDEX
#define DXGK_PRESENT_SOURCE_INDEX 1
#endif

#ifndef DXGK_PRESENT_DESTINATION_INDEX
#define DXGK_PRESENT_DESTINATION_INDEX 2
#endif

typedef struct _DXGKARG_PRESENT
{
    PVOID                       pDmaBuffer;
    UINT                        DmaSize;
    PVOID                       pDmaBufferPrivateData;
    UINT                        DmaBufferPrivateDataSize;
    union
    {
        DXGK_ALLOCATIONLIST        *pAllocationList;
        PVOID                       pAllocationInfo;
        PVOID                       pPresentMultiPlaneOverlayInfo;
    };
    D3DDDI_PATCHLOCATIONLIST   *pPatchLocationListOut;
    UINT                        PatchLocationListOutSize;
    UINT                        MultipassOffset;
    UINT                        Color;
    RECT                        DstRect;
    RECT                        SrcRect;
    UINT                        SubRectCnt;
    CONST RECT                 *pDstSubRects;
    D3DDDI_FLIPINTERVAL_TYPE    FlipInterval;
    DXGK_PRESENTFLAGS           Flags;
    UINT                        DmaBufferSegmentId;
    PHYSICAL_ADDRESS            DmaBufferPhysicalAddress;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    UINT                        Reserved;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    D3DGPU_VIRTUAL_ADDRESS      DmaBufferGpuVirtualAddress;
    UINT                        NumSrcAllocations;
    UINT                        NumDstAllocations;
    UINT                        PrivateDriverDataSize;
    PVOID                       pPrivateDriverData;
#endif
} DXGKARG_PRESENT, *PDXGKARG_PRESENT;

typedef struct _DXGKARG_RENDER
{
    CONST VOID                 *pCommand;
    UINT                        CommandLength;
    PVOID                       pDmaBuffer;
    UINT                        DmaSize;
    PVOID                       pDmaBufferPrivateData;
    UINT                        DmaBufferPrivateDataSize;
    DXGK_ALLOCATIONLIST        *pAllocationList;
    UINT                        AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST   *pPatchLocationListIn;
    UINT                        PatchLocationListInSize;
    D3DDDI_PATCHLOCATIONLIST   *pPatchLocationListOut;
    UINT                        PatchLocationListOutSize;
    UINT                        MultipassOffset;
    UINT                        DmaBufferSegmentId;
    PHYSICAL_ADDRESS            DmaBufferPhysicalAddress;
} DXGKARG_RENDER, *PDXGKARG_RENDER;


/* =========================================================================
 * DXGKARG_ACQUIRESWIZZLINGRANGE / DXGKARG_RELEASESWIZZLINGRANGE
 * =========================================================================
 */
typedef struct _DXGKARG_ACQUIRESWIZZLINGRANGE
{
    HANDLE  hAllocation;
    UINT    PrivateDriverData;
    UINT    RangeId;        /* out */
    UINT    SegmentId;
    UINT    RangeAddress;   /* out: CPU virtual address */
} DXGKARG_ACQUIRESWIZZLINGRANGE, *PDXGKARG_ACQUIRESWIZZLINGRANGE;

typedef struct _DXGKARG_RELEASESWIZZLINGRANGE
{
    HANDLE  hAllocation;
    UINT    PrivateDriverData;
    UINT    RangeId;
} DXGKARG_RELEASESWIZZLINGRANGE, *PDXGKARG_RELEASESWIZZLINGRANGE;


/* =========================================================================
 * DXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT
 * =========================================================================
 */
typedef struct _DXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    UINT                            PrivateDriverFormatAttribute;
} DXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT, *PDXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT;


/* =========================================================================
 * DXGKARG_QUERYVIDPNHWCAPABILITY  (WDDM 1.1 / Win7)
 * =========================================================================
 */
typedef struct _DXGK_VIDPN_HW_CAPABILITY
{
    union
    {
        struct
        {
            UINT    DriverRotation                  : 1;
            UINT    DriverScaling                   : 1;
            UINT    DriverCloning                   : 1;
            UINT    DriverColorConvert              : 1;
            UINT    DriverLinkedAdapaterOutput      : 1;
            UINT    DriverRemoteDisplay             : 1;
            UINT    Reserved                        : 26;
        };
        UINT    Value;
    };
} DXGK_VIDPN_HW_CAPABILITY;

typedef struct _DXGKARG_QUERYVIDPNHWCAPABILITY
{
    D3DKMDT_HVIDPN                  hFunctionalVidPn;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  SourceId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID  TargetId;
    DXGK_VIDPN_HW_CAPABILITY        VidPnHWCaps;    /* out */
} DXGKARG_QUERYVIDPNHWCAPABILITY, *PDXGKARG_QUERYVIDPNHWCAPABILITY;


/* =========================================================================
 * DXGKARG_GETMULTISAMPLEANALYSISMASK
 * =========================================================================
 */
typedef struct _DXGKARG_GETMULTISAMPLEANALYSISMASK
{
    HANDLE  hAllocation;
    UINT    SubResourceIndex;
    UINT    MultiSampleCount;
    UINT    AnalysisMask;   /* out */
} DXGKARG_GETMULTISAMPLEANALYSISMASK, *PDXGKARG_GETMULTISAMPLEANALYSISMASK;


/* =========================================================================
 * DXGKARG_QUERYENGINESTATUS / DXGKARG_RESETENGINE  (WDDM 1.1 / Win7 TDR)
 * =========================================================================
 */
typedef struct _DXGKARG_QUERYENGINESTATUS
{
    UINT        NodeOrdinal;
    UINT        EngineOrdinal;
    BOOLEAN     Responsive;     /* out */
} DXGKARG_QUERYENGINESTATUS, *PDXGKARG_QUERYENGINESTATUS;

typedef struct _DXGKARG_RESETENGINE
{
    UINT    NodeOrdinal;
    UINT    EngineOrdinal;
    UINT    LastAbortedFenceId; /* out */
} DXGKARG_RESETENGINE, *PDXGKARG_RESETENGINE;


/* =========================================================================
 * DXGKARG_QUERYPHYSICALADAPTERINFO  (WDDM 2.0+ placeholder)
 * =========================================================================
 */
typedef struct _DXGKARG_QUERYPHYSICALADAPTERINFO
{
    UINT    Type;
    PVOID   pOutputData;
    UINT    OutputDataSize;
} DXGKARG_QUERYPHYSICALADAPTERINFO, *PDXGKARG_QUERYPHYSICALADAPTERINFO;


/* =========================================================================
 * DXGKARGCB_NOTIFY_INTERRUPT_DATA
 *
 * Describes the GPU event passed to DxgkCbNotifyInterrupt.
 * =========================================================================
 */
typedef enum _DXGK_PRESENT_DISPLAY_ONLY_PROGRESS_ID
{
    DXGK_PRESENT_DISPLAYONLY_PROGRESS_ID_COMPLETE = 0,
    DXGK_PRESENT_DISPLAYONLY_PROGRESS_ID_FAILED = 1,
} DXGK_PRESENT_DISPLAY_ONLY_PROGRESS_ID;

typedef struct _DXGKARGCB_PRESENT_DISPLAYONLY_PROGRESS
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId;
    DXGK_PRESENT_DISPLAY_ONLY_PROGRESS_ID ProgressId;
} DXGKARGCB_PRESENT_DISPLAYONLY_PROGRESS;

typedef struct _DXGKCB_NOTIFY_INTERRUPT_DATA_FLAGS
{
    union
    {
        struct
        {
            UINT ValidPhysicalAdapterMask : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
            UINT HsyncFlipCompletion : 1;
            UINT Reserved : 30;
#else
            UINT Reserved : 31;
#endif
        };
        UINT Value;
    };
} DXGKCB_NOTIFY_INTERRUPT_DATA_FLAGS;

typedef struct _DXGKCB_NOTIFY_MPO_VSYNC_FLAGS
{
    union
    {
        struct
        {
            UINT PostPresentNeeded : 1;
            UINT Reserved : 31;
        };
        UINT Value;
    };
} DXGKCB_NOTIFY_MPO_VSYNC_FLAGS;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
typedef struct _DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
typedef struct _DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO2
{
    DWORD                             LayerIndex;
    ULONGLONG                         PresentId;
    DXGKCB_NOTIFY_MPO_VSYNC_FLAGS     Flags;
} DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO2;
#endif

typedef struct _DXGKARGCB_NOTIFY_INTERRUPT_DATA
{
    DXGK_INTERRUPT_TYPE InterruptType;
    union
    {
        struct
        {
            UINT SubmissionFenceId;
            UINT NodeOrdinal;
            UINT EngineOrdinal;
        } DmaCompleted;
        struct
        {
            UINT PreemptionFenceId;
            UINT LastCompletedFenceId;
            UINT NodeOrdinal;
            UINT EngineOrdinal;
        } DmaPreempted;
        struct
        {
            UINT     FaultedFenceId;
            NTSTATUS Status;
            UINT     NodeOrdinal;
            UINT     EngineOrdinal;
        } DmaFaulted;
        struct
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
            PHYSICAL_ADDRESS               PhysicalAddress;
            UINT                           PhysicalAdapterMask;
        } CrtcVsync;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
        struct
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
        } DisplayOnlyVsync;
        struct
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID      VidPnTargetId;
            UINT                                PhysicalAdapterMask;
            UINT                                MultiPlaneOverlayVsyncInfoCount;
            DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO *pMultiPlaneOverlayVsyncInfo;
        } CrtcVsyncWithMultiPlaneOverlay;
        DXGKARGCB_PRESENT_DISPLAYONLY_PROGRESS DisplayOnlyPresentProgress;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
        struct
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
            DXGK_MIRACAST_CHUNK_INFO       ChunkInfo;
            PVOID                          pPrivateDriverData;
            UINT                           PrivateDataDriverSize;
            NTSTATUS                       Status;
        } MiracastEncodeChunkCompleted;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        struct
        {
            UINT                       FaultedFenceId;
            UINT64                     FaultedPrimitiveAPISequenceNumber;
            DXGK_RENDER_PIPELINE_STAGE FaultedPipelineStage;
            UINT                       FaultedBindTableEntry;
            DXGK_PAGE_FAULT_FLAGS      PageFaultFlags;
            D3DGPU_VIRTUAL_ADDRESS     FaultedVirtualAddress;
            UINT                       NodeOrdinal;
            UINT                       EngineOrdinal;
            UINT                       PageTableLevel;
            DXGK_FAULT_ERROR_CODE      FaultErrorCode;
            HANDLE                     FaultedProcessHandle;
        } DmaPageFaulted;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        struct
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID       VidPnTargetId;
            UINT                                 PhysicalAdapterMask;
            UINT                                 MultiPlaneOverlayVsyncInfoCount;
            DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO2 *pMultiPlaneOverlayVsyncInfo;
            ULONGLONG                             GpuFrequency;
            ULONGLONG                             GpuClockCounter;
        } CrtcVsyncWithMultiPlaneOverlay2;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
        struct
        {
            UINT NodeOrdinal;
            UINT EngineOrdinal;
        } MonitoredFenceSignaled;
        struct
        {
            UINT   NodeOrdinal;
            UINT   EngineOrdinal;
            UINT64 ContextSwitchFence;
        } HwContextListSwitchCompleted;
        struct
        {
            UINT64                 FaultedFenceId;
            D3DGPU_VIRTUAL_ADDRESS FaultedVirtualAddress;
            UINT64                 FaultedPrimitiveAPISequenceNumber;
            union
            {
                HANDLE FaultedHwQueue;
                HANDLE FaultedHwContext;
                HANDLE FaultedProcessHandle;
            };
            UINT                       NodeOrdinal;
            UINT                       EngineOrdinal;
            DXGK_RENDER_PIPELINE_STAGE FaultedPipelineStage;
            UINT                       FaultedBindTableEntry;
            DXGK_PAGE_FAULT_FLAGS      PageFaultFlags;
            UINT                       PageTableLevel;
            DXGK_FAULT_ERROR_CODE      FaultErrorCode;
        } HwQueuePageFaulted;
        struct
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
            UINT                           NotificationID;
        } PeriodicMonitoredFenceSignaled;
#endif
        struct
        {
            UINT Reserved[16];
        } Reserved;
    };
    DXGKCB_NOTIFY_INTERRUPT_DATA_FLAGS Flags;
} DXGKARGCB_NOTIFY_INTERRUPT_DATA, *PDXGKARGCB_NOTIFY_INTERRUPT_DATA;


/*
 * DXGKARGCB_ALLOCATECONTIGUOUSMEMORY, DXGKARGCB_FREECONTIGUOUSMEMORY,
 * DXGKARGCB_MAPPHYSICALMEMORY, DXGKARGCB_UNMAP_PHYSICAL_MEMORY
 *
 * These structures are internal to dxgkrnl.sys and are defined in
 * dxgkrnl_private.h (which matches the Vista WDK / dispmprt.h layout).
 * They are not part of the public DDI and should not be defined here.
 */


/* =========================================================================
 * Hardware overlay DDI argument structures (optional capability)
 * =========================================================================
 */
typedef struct _DXGK_OVERLAYINFO
{
    HANDLE           hAllocation;
    PHYSICAL_ADDRESS PhysicalAddress;
    UINT             SegmentId;
    RECT             DstRect;
    RECT             SrcRect;
    PVOID            pPrivateDriverData;
    UINT             PrivateDriverDataSize;
} DXGK_OVERLAYINFO;

typedef struct _DXGKARG_CREATEOVERLAY
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    DXGK_OVERLAYINFO                OverlayInfo;
    HANDLE                          hOverlay;   /* out */
} DXGKARG_CREATEOVERLAY, *PDXGKARG_CREATEOVERLAY;

typedef struct _DXGKARG_UPDATEOVERLAY
{
    DXGK_OVERLAYINFO    OverlayInfo;
} DXGKARG_UPDATEOVERLAY, *PDXGKARG_UPDATEOVERLAY;

typedef struct _DXGKARG_FLIPOVERLAY
{
    HANDLE           hSource;
    PHYSICAL_ADDRESS PhysicalAddress;
    UINT             SegmentId;
    PVOID            pPrivateDriverData;
    UINT             PrivateDriverDataSize;
} DXGKARG_FLIPOVERLAY, *PDXGKARG_FLIPOVERLAY;

typedef struct _DXGKARG_DESTROYOVERLAY
{
    HANDLE  hOverlay;
} DXGKARG_DESTROYOVERLAY, *PDXGKARG_DESTROYOVERLAY;


/* =========================================================================
 * DXGK_PRESENT_DISPLAYONLY_PROGRESS_ID constants
 * =========================================================================
 */
#define DXGK_PRESENT_DISPLAYONLY_PROGRESS_ID_COMPLETE   0
#define DXGK_PRESENT_DISPLAYONLY_PROGRESS_ID_FAILED     1


/* =========================================================================
 * WDDM version constants (WDDMVersion field of DXGK_DRIVERCAPS)
 * =========================================================================
 */
#ifndef DXGKDDI_WDDMv1
#define DXGKDDI_WDDMv1      0x1000
#define DXGKDDI_WDDMv1_2    0x1200
#define DXGKDDI_WDDMv1_3    0x1300
#define DXGKDDI_WDDMv2_0    0x2000
#endif

/* =========================================================================
 * DXGK_DISPLAY_DRIVERCAPS_EXTENSION
 *
 * Returned by DxgkDdiQueryAdapterInfo(DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION).
 * Contains extended display driver capabilities for DOD drivers.
 * =========================================================================
 */
typedef struct _DXGK_DISPLAY_DRIVERCAPS_EXTENSION
{
    union
    {
        struct
        {
            UINT VirtualModeSupport : 1;
            UINT Reserved           : 31;
        };
        UINT Value;
    };
} DXGK_DISPLAY_DRIVERCAPS_EXTENSION, *PDXGK_DISPLAY_DRIVERCAPS_EXTENSION;


/* =========================================================================
 * WDDM 2.0 DDI argument structures and function typedefs
 *
 * These definitions enable miniport drivers to implement GPU virtual
 * addressing, per-process GPU state, GDI rendering, CPU host aperture
 * mapping, and multi-plane overlay v2 support.
 *
 * Layout matches Windows 10 WDK (10.0.16299.0) d3dkmddi.h exactly.
 * =========================================================================
 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)

typedef _In_ CONST HANDLE IN_CONST_HANDLE;

typedef struct _DXGK_MULTIPLANE_OVERLAY_FLAGS
{
    union
    {
        struct
        {
            UINT VerticalFlip   : 1;
            UINT HorizontalFlip : 1;
#if defined(DXGKDDI_INTERFACE_VERSION_WDDM3_0) && (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
            UINT StaticCheck    : 1;
            UINT Reserved       : 29;
#else
            UINT Reserved       : 30;
#endif
        };
        UINT Value;
    };
} DXGK_MULTIPLANE_OVERLAY_FLAGS, *PDXGK_MULTIPLANE_OVERLAY_FLAGS;

typedef enum _DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT
{
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_MONO               = 0,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_HORIZONTAL         = 1,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_VERTICAL           = 2,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_SEPARATE           = 3,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_MONO_OFFSET        = 4,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_ROW_INTERLEAVED    = 5,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_COLUMN_INTERLEAVED = 6,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT_CHECKERBOARD       = 7
} DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT;

typedef enum _DXGK_MULTIPLANE_OVERLAY_STEREO_FLIP_MODE
{
    DXGK_MULTIPLANE_OVERLAY_STEREO_FLIP_NONE   = 0,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FLIP_FRAME0 = 1,
    DXGK_MULTIPLANE_OVERLAY_STEREO_FLIP_FRAME1 = 2,
} DXGK_MULTIPLANE_OVERLAY_STEREO_FLIP_MODE;

typedef enum _DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY
{
    DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY_BILINEAR = 0x1,
    DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY_HIGH     = 0x2,
} DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY;

typedef struct _DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_RETURN_INFO
{
    union
    {
        struct
        {
            UINT FailingPlane : 4;
            UINT TryAgain     : 1;
            UINT Reserved     : 27;
        };
        UINT Value;
    };
} DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_RETURN_INFO;

/* --- DxgkDdiSetRootPageTable ------------------------------------------ */

typedef struct _DXGKARG_SETROOTPAGETABLE
{
    HANDLE                   hContext;
    D3DGPU_PHYSICAL_ADDRESS  Address;
    UINT                     NumEntries;
} DXGKARG_SETROOTPAGETABLE;

typedef _In_ CONST DXGKARG_SETROOTPAGETABLE* IN_CONST_PDXGKARG_SETROOTPAGETABLE;

typedef
    VOID
    (APIENTRY *PDXGKDDI_SETROOTPAGETABLE)(
        IN_CONST_HANDLE                     hAdapter,
        IN_CONST_PDXGKARG_SETROOTPAGETABLE  pSetPageTable);

/* --- DxgkDdiGetRootPageTableSize -------------------------------------- */

typedef struct _DXGKARG_GETROOTPAGETABLESIZE
{
    UINT    NumberOfPte;            /* In/Out */
    UINT    PhysicalAdapterIndex;   /* In     */
} DXGKARG_GETROOTPAGETABLESIZE;

typedef _Inout_ DXGKARG_GETROOTPAGETABLESIZE* INOUT_PDXGKARG_GETROOTPAGETABLESIZE;

typedef
    SIZE_T
    (APIENTRY *PDXGKDDI_GETROOTPAGETABLESIZE)(
        IN_CONST_HANDLE                         hAdapter,
        INOUT_PDXGKARG_GETROOTPAGETABLESIZE     pArgs);

/* --- DxgkDdiCreateProcess / DxgkDdiDestroyProcess --------------------- */

typedef struct _DXGK_CREATEPROCESSFLAGS
{
    union
    {
        struct
        {
            UINT    SystemProcess    : 1;
            UINT    GdiProcess       : 1;
            UINT    Reserved         : 30;
        };
        UINT Value;
    };
} DXGK_CREATEPROCESSFLAGS;

typedef struct _DXGKARG_CREATEPROCESS
{
    HANDLE                  hDxgkProcess;   /* in  */
    HANDLE                  hKmdProcess;    /* out */
    DXGK_CREATEPROCESSFLAGS Flags;          /* in  */
    UINT                    NumPasid;       /* in  */
    ULONG*                  pPasid;         /* in  */
} DXGKARG_CREATEPROCESS;

typedef _Inout_ DXGKARG_CREATEPROCESS* INOUT_PDXGKARG_CREATEPROCESS;

typedef
    _Check_return_
    NTSTATUS
    (APIENTRY *PDXGKDDI_CREATEPROCESS)(
        IN_CONST_HANDLE               hAdapter,
        INOUT_PDXGKARG_CREATEPROCESS  pArgs);

typedef
    _Check_return_
    NTSTATUS
    (APIENTRY *PDXGKDDI_DESTROYPROCESS)(
        IN_CONST_HANDLE hAdapter,
        IN_CONST_HANDLE hKmdProcess);

/* --- DxgkDdiSubmitCommandVirtual -------------------------------------- */

typedef struct _DXGKARG_SUBMITCOMMANDVIRTUAL
{
    HANDLE                          hContext;
    D3DGPU_VIRTUAL_ADDRESS          DmaBufferVirtualAddress;
    UINT                            DmaBufferSize;
    VOID*                           pDmaBufferPrivateData;
    UINT                            DmaBufferPrivateDataSize;
    UINT                            DmaBufferUmdPrivateDataSize;
    UINT                            SubmissionFenceId;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    D3DDDI_FLIPINTERVAL_TYPE        FlipInterval;
    DXGK_SUBMITCOMMANDFLAGS         Flags;
    UINT                            EngineOrdinal;
    UINT                            NodeOrdinal;
} DXGKARG_SUBMITCOMMANDVIRTUAL;

typedef _In_ CONST DXGKARG_SUBMITCOMMANDVIRTUAL* IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL;

typedef
    _Check_return_
    NTSTATUS
    (APIENTRY *PDXGKDDI_SUBMITCOMMANDVIRTUAL)(
        IN_CONST_HANDLE                         hAdapter,
        IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL  pSubmitCommand);

/* --- DxgkDdiRenderGdi ------------------------------------------------- */

typedef struct _DXGKARG_RENDERGDI
{
    CONST VOID* CONST           pCommand;
    CONST UINT                  CommandLength;
    VOID*                       pDmaBuffer;
    D3DGPU_VIRTUAL_ADDRESS      DmaBufferGpuVirtualAddress;
    UINT                        DmaSize;
    VOID*                       pDmaBufferPrivateData;
    UINT                        DmaBufferPrivateDataSize;
    DXGK_ALLOCATIONLIST*        pAllocationList;
    UINT                        AllocationListSize;
    UINT                        MultipassOffset;
} DXGKARG_RENDERGDI;

typedef _Inout_ DXGKARG_RENDERGDI* INOUT_PDXGKARG_RENDERGDI;

typedef
    _Check_return_
    NTSTATUS
    (APIENTRY *PDXGKDDI_RENDERGDI)(
        IN_CONST_HANDLE           hContext,
        INOUT_PDXGKARG_RENDERGDI  pRenderGdi);

/* --- DxgkDdiMapCpuHostAperture / DxgkDdiUnmapCpuHostAperture --------- */

typedef struct _DXGKARG_MAPCPUHOSTAPERTURE
{
    HANDLE  hAllocation;
    USHORT  SegmentId;
    USHORT  PhysicalAdapterIndex;
    UINT64  NumberOfPages;
    UINT32* pCpuHostAperturePages;
    UINT64* pMemorySegmentPages;
} DXGKARG_MAPCPUHOSTAPERTURE;

typedef _In_ CONST DXGKARG_MAPCPUHOSTAPERTURE* IN_CONST_PDXGKARG_MAPCPUHOSTAPERTURE;

typedef
    _Check_return_
    NTSTATUS
    (APIENTRY *PDXGKDDI_MAPCPUHOSTAPERTURE)(
        IN_CONST_HANDLE                       hAdapter,
        IN_CONST_PDXGKARG_MAPCPUHOSTAPERTURE  pArgs);

typedef struct _DXGKARG_UNMAPCPUHOSTAPERTURE
{
    UINT64  NumberOfPages;
    UINT32* pCpuHostAperturePages;
    USHORT  SegmentId;
    USHORT  PhysicalAdapterIndex;
} DXGKARG_UNMAPCPUHOSTAPERTURE;

typedef _In_ CONST DXGKARG_UNMAPCPUHOSTAPERTURE* IN_CONST_PDXGKARG_UNMAPCPUHOSTAPERTURE;

typedef
    _Check_return_
    NTSTATUS
    (APIENTRY *PDXGKDDI_UNMAPCPUHOSTAPERTURE)(
        IN_CONST_HANDLE                          hAdapter,
        IN_CONST_PDXGKARG_UNMAPCPUHOSTAPERTURE   pArgs);

/* --- DxgkDdiCheckMultiPlaneOverlaySupport2 ---------------------------- */

/* =========================================================================
 * Multi-plane overlay (MPO) — layouts match the genuine WDK d3dkmddi.h
 * (winsdk-10 10.0.16299) verbatim for the WDDM1.3-era DDI set.
 * ========================================================================= */
typedef struct _DXGK_MULTIPLANE_OVERLAY_BLEND
{
    union
    {
        struct
        {
            UINT    AlphaBlend     : 1;
            UINT    Reserved       :31;
        };
        UINT Value;
    };
} DXGK_MULTIPLANE_OVERLAY_BLEND;

typedef struct _DXGK_MULTIPLANE_OVERLAY_YCbCr_FLAGS
{
    union
    {
        struct
        {
            UINT    NominalRange   : 1;
            UINT    Bt709          : 1;
            UINT    xvYCC          : 1;
            UINT    Reserved       : 29;
        };
        UINT Value;
    };
} DXGK_MULTIPLANE_OVERLAY_YCbCr_FLAGS;

typedef enum _DXGK_MULTIPLANE_OVERLAY_VIDEO_FRAME_FORMAT
{
    DXGK_MULTIPLANE_OVERLAY_VIDEO_FRAME_FORMAT_PROGRESSIVE                   = 0,
    DXGK_MULTIPLANE_OVERLAY_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST    = 1,
    DXGK_MULTIPLANE_OVERLAY_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST = 2,
} DXGK_MULTIPLANE_OVERLAY_VIDEO_FRAME_FORMAT;

typedef struct _DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES
{
    DXGK_MULTIPLANE_OVERLAY_FLAGS                  Flags;
    RECT                                           SrcRect;
    RECT                                           DstRect;
    RECT                                           ClipRect;
    D3DDDI_ROTATION                                Rotation;
    DXGK_MULTIPLANE_OVERLAY_BLEND                  Blend;
    DXGK_MULTIPLANE_OVERLAY_VIDEO_FRAME_FORMAT     VideoFrameFormat;
    DXGK_MULTIPLANE_OVERLAY_YCbCr_FLAGS            YCbCrFlags;
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT          StereoFormat;
    BOOL                                           StereoLeftViewFrame0;
    BOOL                                           StereoBaseViewFrame0;
    DXGK_MULTIPLANE_OVERLAY_STEREO_FLIP_MODE       StereoFlipMode;
    DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY        StretchQuality;
} DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES;

struct _DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO
{
    DWORD                              LayerIndex;
    BOOL                               Enabled;
    PHYSICAL_ADDRESS                   PhysicalAddress;
    DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES PlaneAttributes;
};

typedef struct _DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_PLANE
{
    HANDLE                               hAllocation;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID       VidPnSourceId;
    DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES   PlaneAttributes;
} DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_PLANE;

typedef struct _DXGK_MULTIPLANE_OVERLAY_PLANE
{
    UINT                                 LayerIndex;
    BOOL                                 Enabled;
    UINT                                 AllocationSegment;
    PHYSICAL_ADDRESS                     AllocationAddress;
    HANDLE                               hAllocation;
    DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES   PlaneAttributes;
} DXGK_MULTIPLANE_OVERLAY_PLANE;

typedef struct _DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID                     VidPnSourceId;
    UINT                                               PlaneCount;
    DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_PLANE       *pOverlayPlanes;
    BOOLEAN                                            Supported;
    DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_RETURN_INFO  ReturnInfo;
} DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT, *PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT;

typedef struct _DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY
{
    UINT                             ContextCount;
    HANDLE                           Context[1+D3DDDI_MAX_BROADCAST_CONTEXT];
    DXGK_SETVIDPNSOURCEADDRESS_FLAGS Flags;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID   VidPnSourceId;
    UINT                             PlaneCount;
    DXGK_MULTIPLANE_OVERLAY_PLANE   *pPlanes;
    UINT                             Duration;
} DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY,
 *PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY;

typedef NTSTATUS
(APIENTRY *PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT)(
    _In_    PVOID                                   MiniportDeviceContext,
    _Inout_ PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT  CheckMultiPlaneOverlaySupport);

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY)(
    _In_ PVOID                                                    MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY *SetVidPnSourceAddressWithMpo);

typedef struct _DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES2
{
    DXGK_MULTIPLANE_OVERLAY_FLAGS            Flags;
    RECT                                     SrcRect;
    RECT                                     DstRect;
    RECT                                     ClipRect;
    D3DDDI_ROTATION                          Rotation;
    D3DDDI_MULTISAMPLINGMETHOD               BlendType;
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT    StereoFormat;
    BOOL                                     StereoLeftViewFrame0;
    BOOL                                     StereoBaseViewFrame0;
    DXGK_MULTIPLANE_OVERLAY_STEREO_FLIP_MODE StereoFlipMode;
    DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY  StretchQuality;
    UINT                                     Reserved1;
} DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES2;

typedef struct _DXGK_MULTIPLANE_OVERLAY_PLANE_WITH_SOURCE
{
    HANDLE                                  hAllocation;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID          VidPnSourceId;
    UINT                                    LayerIndex;
    DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES2     PlaneAttributes;
} DXGK_MULTIPLANE_OVERLAY_PLANE_WITH_SOURCE;

typedef struct _DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2
{
    UINT                                                PlaneCount;
    DXGK_MULTIPLANE_OVERLAY_PLANE_WITH_SOURCE*          pPlanes;
    BOOL                                                Supported;
    DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_RETURN_INFO   ReturnInfo;
} DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2;

typedef _Inout_ DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2* IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2;

typedef
    _Check_return_
    NTSTATUS
    (APIENTRY *PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT2)(
        IN_CONST_HANDLE                                  hAdapter,
        IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2   pCheckMultiPlaneOverlaySupport);

/* --- DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2 --------------- */

typedef struct _DXGK_MULTIPLANE_OVERLAY_PLANE2
{
    UINT                                 LayerIndex;
    BOOL                                 Enabled;
    UINT                                 AllocationSegment;
    PHYSICAL_ADDRESS                     AllocationAddress;
    HANDLE                               hAllocation;
    DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES2  PlaneAttributes;
} DXGK_MULTIPLANE_OVERLAY_PLANE2;

typedef struct _DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2
{
    UINT                             ContextCount;
    HANDLE                           Context[1+D3DDDI_MAX_BROADCAST_CONTEXT];
    DXGK_SETVIDPNSOURCEADDRESS_FLAGS Flags;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID   VidPnSourceId;
    UINT                             PlaneCount;
    DXGK_MULTIPLANE_OVERLAY_PLANE2*  pPlanes;
    UINT                             Duration;
} DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2;

typedef _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2*
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2;

typedef
    _Check_return_
    NTSTATUS
    (APIENTRY *PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2)(
        IN_CONST_HANDLE hAdapter,
        IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2
            pSetVidPnSourceAddressWithMultiPlaneOverlay);

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0 */

#if defined(_WIN64) && (DXGKDDI_INTERFACE_VERSION == DXGKDDI_INTERFACE_VERSION_VISTA)
C_ASSERT(sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA) == 80);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, InterruptType) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaCompleted.SubmissionFenceId) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, CrtcVsync.PhysicalAddress) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, CrtcVsync.PhysicalAdapterMask) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, Flags) == 72);
#endif

#if defined(_WIN64) && (DXGKDDI_INTERFACE_VERSION == 0x5023)
C_ASSERT(sizeof(D3DDDI_ALLOCATIONLIST) == 8);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONLIST, hAllocation) == 0);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONLIST, Value) == 4);
C_ASSERT(sizeof(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA) == 48);
C_ASSERT(FIELD_OFFSET(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, StandardAllocationType) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, pCreateSharedPrimarySurfaceData) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, pAllocationPrivateDriverData) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, AllocationPrivateDriverDataSize) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, pResourcePrivateDriverData) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, ResourcePrivateDriverDataSize) == 40);
C_ASSERT(FIELD_OFFSET(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, PhysicalAdapterIndex) == 44);
C_ASSERT(sizeof(DXGKARG_RECOMMENDFUNCTIONALVIDPN) == 48);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDFUNCTIONALVIDPN, NumberOfVidPnTargets) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDFUNCTIONALVIDPN, pVidPnTargetPrioritizationVector) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDFUNCTIONALVIDPN, hRecommendedFunctionalVidPn) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDFUNCTIONALVIDPN, RequestReason) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDFUNCTIONALVIDPN, pPrivateDriverData) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDFUNCTIONALVIDPN, PrivateDriverDataSize) == 40);
C_ASSERT(sizeof(DXGK_ENUM_PIVOT) == 8);
C_ASSERT(FIELD_OFFSET(DXGK_ENUM_PIVOT, VidPnSourceId) == 0);
C_ASSERT(FIELD_OFFSET(DXGK_ENUM_PIVOT, VidPnTargetId) == 4);
C_ASSERT(sizeof(DXGKARG_ENUMVIDPNCOFUNCMODALITY) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_ENUMVIDPNCOFUNCMODALITY, hConstrainingVidPn) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARG_ENUMVIDPNCOFUNCMODALITY, EnumPivotType) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_ENUMVIDPNCOFUNCMODALITY, EnumPivot) == 12);
C_ASSERT(sizeof(DXGKARG_RECOMMENDVIDPNTOPOLOGY) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDVIDPNTOPOLOGY, hVidPn) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDVIDPNTOPOLOGY, VidPnSourceId) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDVIDPNTOPOLOGY, RequestReason) == 12);
C_ASSERT(FIELD_OFFSET(DXGKARG_RECOMMENDVIDPNTOPOLOGY, hFallbackTopology) == 16);
C_ASSERT(sizeof(DXGKARG_ESCAPE) == 40);
C_ASSERT(FIELD_OFFSET(DXGKARG_ESCAPE, hDevice) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARG_ESCAPE, Flags) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_ESCAPE, pPrivateDriverData) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_ESCAPE, PrivateDriverDataSize) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_ESCAPE, hContext) == 32);
C_ASSERT(sizeof(DXGK_PRIMARYDATA) == 24);
C_ASSERT(FIELD_OFFSET(DXGK_PRIMARYDATA, hAllocation) == 0);
C_ASSERT(FIELD_OFFSET(DXGK_PRIMARYDATA, SegmentId) == 8);
C_ASSERT(FIELD_OFFSET(DXGK_PRIMARYDATA, SegmentAddress) == 16);
C_ASSERT(sizeof(DXGKARG_SETVIDPNSOURCEADDRESS) == 2112);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, VidPnSourceId) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, PrimarySegment) == 4);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, PrimaryAddress) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, hAllocation) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, ContextCount) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, Context) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, Flags) == 552);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, Duration) == 556);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, PrimaryData) == 560);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, DriverPrivateDataSize) == 2096);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESS, pDriverPrivateData) == 2104);
C_ASSERT(sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA) == 80);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, InterruptType) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaCompleted.SubmissionFenceId) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaCompleted.NodeOrdinal) == 12);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaCompleted.EngineOrdinal) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPreempted.PreemptionFenceId) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPreempted.LastCompletedFenceId) == 12);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPreempted.NodeOrdinal) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPreempted.EngineOrdinal) == 20);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaFaulted.FaultedFenceId) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaFaulted.Status) == 12);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaFaulted.NodeOrdinal) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaFaulted.EngineOrdinal) == 20);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, CrtcVsync.VidPnTargetId) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, CrtcVsync.PhysicalAddress) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, CrtcVsync.PhysicalAdapterMask) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.FaultedFenceId) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.FaultedPrimitiveAPISequenceNumber) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.FaultedPipelineStage) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.FaultedBindTableEntry) == 28);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.PageFaultFlags) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.FaultedVirtualAddress) == 40);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.NodeOrdinal) == 48);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.EngineOrdinal) == 52);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.PageTableLevel) == 56);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.FaultErrorCode) == 60);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaPageFaulted.FaultedProcessHandle) == 64);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, Flags) == 72);
C_ASSERT(sizeof(DXGK_GPUENGINETOPOLOGY) == 260);
C_ASSERT(sizeof(DXGK_DRIVERCAPS) == 576);
C_ASSERT(sizeof(DXGKARG_CREATEDEVICE) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEDEVICE, hDevice) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEDEVICE, Flags) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEDEVICE, Pasid) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEDEVICE, hKmdProcess) == 24);
C_ASSERT(sizeof(DXGK_CONTEXTINFO) == 32);
C_ASSERT(FIELD_OFFSET(DXGK_CONTEXTINFO, Reserved) == 20);
C_ASSERT(FIELD_OFFSET(DXGK_CONTEXTINFO, Caps) == 24);
C_ASSERT(FIELD_OFFSET(DXGK_CONTEXTINFO, PagingCompanionNodeId) == 28);
C_ASSERT(sizeof(DXGKARG_CREATECONTEXT) == 72);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECONTEXT, hContext) == 0);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECONTEXT, NodeOrdinal) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECONTEXT, EngineAffinity) == 12);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECONTEXT, Flags) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECONTEXT, pPrivateDriverData) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECONTEXT, PrivateDriverDataSize) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECONTEXT, ContextInfo) == 36);
C_ASSERT(sizeof(DXGKARG_DESTROYALLOCATION) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_DESTROYALLOCATION, pAllocationList) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_DESTROYALLOCATION, hResource) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_DESTROYALLOCATION, Flags) == 24);
C_ASSERT(sizeof(DXGKARG_OPENALLOCATION) == 56);
C_ASSERT(FIELD_OFFSET(DXGKARG_OPENALLOCATION, pOpenAllocation) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_OPENALLOCATION, pPrivateDriverData) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_OPENALLOCATION, PrivateDriverSize) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_OPENALLOCATION, Flags) == 28);
C_ASSERT(FIELD_OFFSET(DXGKARG_OPENALLOCATION, SubresourceIndex) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_OPENALLOCATION, SubresourceOffset) == 40);
C_ASSERT(FIELD_OFFSET(DXGKARG_OPENALLOCATION, Pitch) == 48);
C_ASSERT(sizeof(DXGKARG_PRESENT) == 168);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, pAllocationList) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, pPatchLocationListOut) == 40);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, MultipassOffset) == 52);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, DstRect) == 60);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, pDstSubRects) == 96);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, FlipInterval) == 104);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, Flags) == 108);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, DmaBufferSegmentId) == 112);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, DmaBufferPhysicalAddress) == 120);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, Reserved) == 128);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, DmaBufferGpuVirtualAddress) == 136);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, NumSrcAllocations) == 144);
C_ASSERT(FIELD_OFFSET(DXGKARG_PRESENT, pPrivateDriverData) == 160);
C_ASSERT(sizeof(DXGKARG_SUBMITCOMMAND) == 96);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, DmaBufferSegmentId) == 8);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, DmaBufferPhysicalAddress) == 16);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, DmaBufferSize) == 24);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, DmaBufferSubmissionStartOffset) == 28);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, pDmaBufferPrivateData) == 40);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, DmaBufferPrivateDataSize) == 48);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, SubmissionFenceId) == 60);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, VidPnSourceId) == 64);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, FlipInterval) == 68);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, Flags) == 72);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, EngineOrdinal) == 76);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, DmaBufferVirtualAddress) == 80);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMAND, NodeOrdinal) == 88);
C_ASSERT(sizeof(DXGKARG_BUILDPAGINGBUFFER) == 320);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Operation) == 28);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, MultipassOffset) == 32);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.hAllocation) == 40);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.TransferOffset) == 48);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.TransferSize) == 56);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.Source.SegmentId) == 64);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.Source.SegmentAddress) == 72);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.Destination.SegmentId) == 80);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.Destination.SegmentAddress) == 88);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.Flags) == 96);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Transfer.MdlOffset) == 100);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, hSystemContext) == 296);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, DmaBufferGpuVirtualAddress) == 304);
C_ASSERT(FIELD_OFFSET(DXGKARG_BUILDPAGINGBUFFER, DmaBufferWriteOffset) == 312);
#endif


#pragma warning(pop)

#endif /* _D3DKMDDI_H_ */
