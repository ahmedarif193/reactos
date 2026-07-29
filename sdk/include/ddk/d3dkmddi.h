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

#ifndef _Function_class_DXGK_
#ifdef ENABLE_DXGK_SAL
#define _Function_class_DXGK_(param) _Function_class_(param)
#else
#define _Function_class_DXGK_(param)
#endif
#endif

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
            UINT    ShareBackingStoreWithKmd : 1;   // 0x00000001
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
            UINT    NoImplicitSynchronization : 1;  // 0x00000002
            UINT    DisablePartialResidency   : 1;  // 0x00000004
            UINT    RestrictedToSingleSegment : 1;  // 0x00000008
            UINT    NotifyEviction             : 1;  // 0x00000010
            UINT    NotifyIoMmuUnmap            : 1;  // 0x00000020
            UINT    Reserved                   :26;  // 0xFFFFFFC0
#else
            UINT    Reserved                   :31;  // 0xFFFFFFFE
#endif
        };
        UINT Value;
    };
} DXGK_ALLOCATIONINFOFLAGS2;
C_ASSERT(sizeof(DXGK_ALLOCATIONINFOFLAGS2) == 0x4);
#endif // DXGKDDI_INTERFACE_VERSION_WDDM3_0

typedef struct _DXGK_ALLOCATIONINFO
{
    VOID*                      pPrivateDriverData;
    UINT                       PrivateDriverDataSize;
    union
    {
        UINT Alignment;
        struct
        {
            UINT16 MinimumPageSize;
            UINT16 RecommendedPageSize;
        };
    };
    SIZE_T                     Size;
    SIZE_T                     PitchAlignedSize;
    DXGK_SEGMENTBANKPREFERENCE HintedBank;
    DXGK_SEGMENTPREFERENCE     PreferredSegment;
    union
    {
        UINT SupportedReadSegmentSet;
        UINT MmuSet;
    };
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
        DXGK_ALLOCATIONINFOFLAGS_WDDM2_0 FlagsWddm2;
    };
    DXGK_ALLOCATIONUSAGEHINT* pAllocationUsageHint;
    UINT                      AllocationPriority;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
#if defined(_AMD64_) || defined(_ARM64_)
    DXGK_ALLOCATIONINFOFLAGS2 Flags2;
#endif
#endif
} DXGK_ALLOCATIONINFO;

#ifdef _WIN64
/* Flags2 occupies the 64-bit tail hole after AllocationPriority, so the
 * structure does not grow; the native 32-bit contract omits Flags2. */
C_ASSERT(sizeof(DXGK_ALLOCATIONINFO) == 0x58);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0) && (defined(_AMD64_) || defined(_ARM64_))
C_ASSERT(FIELD_OFFSET(DXGK_ALLOCATIONINFO, Flags2) == 0x54);
#endif
#else
C_ASSERT(sizeof(DXGK_ALLOCATIONINFO) == 0x3C);
#endif /* _WIN64 */


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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    DXGK_INTERRUPT_SCHEDULING_LOG_INTERRUPT = 15,
    DXGK_INTERRUPT_GPU_ENGINE_TIMEOUT = 16,
    DXGK_INTERRUPT_SUSPEND_CONTEXT_COMPLETED = 17,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY3 = 18,
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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    DXGKQAITYPE_RESERVED                  = 3,
    DXGKQAITYPE_QUERYSEGMENT2             = 4,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    DXGKQAITYPE_QUERYSEGMENT3             = 5,
    DXGKQAITYPE_NUMPOWERCOMPONENTS        = 6,
    DXGKQAITYPE_POWERCOMPONENTINFO        = 7,
    DXGKQAITYPE_PREFERREDGPUNODE          = 8,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    DXGKQAITYPE_POWERCOMPONENTPSTATEINFO  = 9,
    DXGKQAITYPE_HISTORYBUFFERPRECISION    = 10,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    DXGKQAITYPE_QUERYSEGMENT4             = 11,
    DXGKQAITYPE_SEGMENTMEMORYSTATE        = 12,
    DXGKQAITYPE_GPUMMUCAPS                = 13,
    DXGKQAITYPE_PAGETABLELEVELDESC        = 14,
    DXGKQAITYPE_PHYSICALADAPTERCAPS       = 15,
    DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION  = 16,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    DXGKQAITYPE_INTEGRATED_DISPLAY_DESCRIPTOR = 17,
    DXGKQAITYPE_UEFIFRAMEBUFFERRANGES         = 18,
    DXGKQAITYPE_QUERYCOLORIMETRYOVERRIDES     = 19,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    DXGKQAITYPE_DISPLAYID_DESCRIPTOR          = 20,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    DXGKQAITYPE_FRAMEBUFFERSAVESIZE            = 21,
    DXGKQAITYPE_HARDWARERESERVEDRANGES         = 22,
    DXGKQAITYPE_INTEGRATED_DISPLAY_DESCRIPTOR2 = 23,
    DXGKQAITYPE_NODEPERFDATA                   = 24,
    DXGKQAITYPE_ADAPTERPERFDATA                = 25,
    DXGKQAITYPE_ADAPTERPERFDATA_CAPS           = 26,
    DXGKQAITYPE_GPUVERSION                     = 27,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    DXGKQAITYPE_DEVICE_TYPE_CAPS                = 28,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
    DXGKQAITYPE_WDDMDEVICECAPS                  = 29,
    DXGKQAITYPE_GPUPCAPS                        = 30,
    DXGKQAITYPE_QUERYTARGETGAMMACAPS            = 31,
    DXGKQAITYPE_SCANOUT_CAPS                    = 33,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    DXGKQAITYPE_PHYSICAL_MEMORY_CAPS             = 34,
    DXGKQAITYPE_IOMMU_CAPS                       = 35,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
    DXGKQAITYPE_HARDWARERESERVEDRANGES2          = 36,
    DXGKQAITYPE_NATIVE_FENCE_CAPS                = 37,
    DXGKQAITYPE_USERMODESUBMISSION_CAPS          = 38,
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
    DXGKQAITYPE_DIRTYBITTRACKINGCAPS             = 39,
    DXGKQAITYPE_DIRTYBITTRACKINGSEGMENTCAPS      = 40,
    DXGKQAITYPE_SCATTER_RESERVE                  = 41,
    DXGKQAITYPE_QUERYPAGINGBUFFERINFO            = 42,
    DXGKQAITYPE_QUERYSEGMENTCOUNT                = 43,
    DXGKQAITYPE_QUERYSEGMENT5                    = 44,
    DXGKQAITYPE_QUERYMMUCOUNT                    = 45,
    DXGKQAITYPE_QUERYMMUS                        = 46,
#endif
    DXGKQAITYPE_64BITONLYCAPS                    = 47,
    DXGKQAITYPE_PAGINGPROCESSGPUVASIZE           = 48,
} DXGK_QUERYADAPTERINFOTYPE;

C_ASSERT(DXGKQAITYPE_64BITONLYCAPS == 47);
C_ASSERT(DXGKQAITYPE_PAGINGPROCESSGPUVASIZE == 48);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
C_ASSERT(DXGKQAITYPE_DIRTYBITTRACKINGCAPS == 39);
C_ASSERT(DXGKQAITYPE_QUERYMMUS == 46);
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)

typedef struct _DXGK_PHYSICAL_MEMORY_CAPS
{
    PHYSICAL_ADDRESS HighestVisibleAddress;
} DXGK_PHYSICAL_MEMORY_CAPS;

typedef struct _DXGK_IOMMU_CAPS
{
    union
    {
        struct
        {
            UINT32 IommuIsolationSupported   : 1;
            UINT32 IommuIsolationRequired    : 1;
            UINT32 DmaRemappingSupported     : 1;
            UINT32 GpuVaIommuRequired        : 1;
            UINT32 GpuVaIommuGlobalRequired  : 1;
            UINT32 Reserved                  : 27;
        };
        UINT32 Value;
    };
} DXGK_IOMMU_CAPS;

typedef UINT_PTR DXGK_PAGE_NUMBER;

typedef struct _DXGK_ADL_FLAGS
{
    union
    {
        struct
        {
            UINT32 Contiguous : 1;
            UINT32 Reserved   : 31;
        };
        UINT32 Value;
    };
} DXGK_ADL_FLAGS;

typedef struct _DXGK_ADL
{
    UINT32         PageCount;
    DXGK_ADL_FLAGS Flags;
    union
    {
        DXGK_PAGE_NUMBER        BasePageNumber;
        const DXGK_PAGE_NUMBER *Pages;
    };
} DXGK_ADL;

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9 */


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


typedef struct _DXGK_GAMMARAMPCAPS
{
    union
    {
        struct
        {
            UINT Gamma_Rgb256x3x16 : 1;
            UINT Reserved          : 31;
        };
        UINT Value;
    };
} DXGK_GAMMARAMPCAPS;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
typedef struct _DXGK_COLORTRANSFORMCAPS
{
    union
    {
        struct
        {
            UINT Gamma_Rgb256x3x16 : 1;
            UINT Gamma_Dxgi1       : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
            UINT Transform_3x4Matrix           : 1;
            UINT Transform_3x4Matrix_WideColor : 1;
            UINT Transform_3x4Matrix_HighColor : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
            UINT Transform_Matrix_V2 : 1;
            UINT Reserved            : 26;
#else
            UINT Reserved            : 27;
#endif
#else
            UINT Reserved            : 30;
#endif
        };
        UINT Value;
    };
} DXGK_COLORTRANSFORMCAPS;
#endif

typedef struct _DXGK_PRESENTATIONCAPS
{
    union
    {
        struct
        {
            UINT NoScreenToScreenBlt              : 1;
            UINT NoOverlapScreenBlt               : 1;
            UINT SupportKernelModeCommandBuffer   : 1;
            UINT NoSameBitmapAlphaBlend           : 1;
            UINT NoSameBitmapStretchBlt           : 1;
            UINT NoSameBitmapTransparentBlt       : 1;
            UINT NoSameBitmapOverlappedAlphaBlend : 1;
            UINT NoSameBitmapOverlappedStretchBlt : 1;
            UINT DriverSupportsCddDwmInterop      : 1;
            UINT Reserved0                        : 1;
            UINT AlignmentShift                   : 4;
            UINT MaxTextureWidthShift             : 3;
            UINT MaxTextureHeightShift            : 3;
            UINT SupportAllBltRops                : 1;
            UINT SupportMirrorStretchBlt          : 1;
            UINT SupportMonoStretchBltModes       : 1;
            UINT StagingRectStartPitchAligned     : 1;
            UINT NoSameBitmapBitBlt               : 1;
            UINT NoSameBitmapOverlappedBitBlt     : 1;
            UINT Reserved1                        : 1;
            UINT NoTempSurfaceForClearTypeBlend   : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
            UINT SupportSoftwareDeviceBitmaps     : 1;
            UINT NoCacheCoherentApertureMemory    : 1;
            UINT SupportLinearHeap                : 1;
            UINT Reserved                         : 1;
#else
            UINT Reserved                         : 4;
#endif
        };
        UINT Value;
    };
} DXGK_PRESENTATIONCAPS, *PDXGK_PRESENTATIONCAPS;

typedef DXGK_PRESENTATIONCAPS DXGK_PRESENTCAPS;
typedef DXGK_PRESENTATIONCAPS *PDXGK_PRESENTCAPS;

typedef struct _DXGK_FLIPCAPS
{
    union
    {
        struct
        {
            UINT FlipOnVSyncWithNoWait : 1;
            UINT FlipOnVSyncMmIo       : 1;
            UINT FlipInterval          : 1;
            UINT FlipImmediateMmIo     : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
            UINT FlipIndependent       : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
            UINT DdiPresentForIFlip    : 1;
            UINT FlipImmediateOnHSync  : 1;
            UINT Reserved              : 25;
#else
            UINT Reserved              : 27;
#endif
#else
            UINT Reserved              : 28;
#endif
        };
        UINT Value;
    };
} DXGK_FLIPCAPS, *PDXGK_FLIPCAPS;

typedef struct _DXGK_VIDSCHCAPS
{
    union
    {
        struct
        {
            UINT MultiEngineAware    : 1;
            UINT VSyncPowerSaveAware : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
            UINT PreemptionAware     : 1;
            UINT NoDmaPatching       : 1;
            UINT CancelCommandAware  : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
            UINT No64BitAtomics      : 1;
            UINT LowIrqlPreemptCommand : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
            UINT HwQueuePacketCap    : 4;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
            UINT NativeGpuFence      : 1;
            UINT OptimizedNativeFenceSignaledInterrupt : 1;
            UINT Reserved            : 19;
#else
            UINT Reserved            : 21;
#endif
#else
            UINT Reserved            : 25;
#endif
#else
            UINT Reserved            : 27;
#endif
#else
            UINT Reserved            : 30;
#endif
        };
        UINT Value;
    };
} DXGK_VIDSCHCAPS, *PDXGK_VIDSCHCAPS;

typedef DXGK_VIDSCHCAPS DXGK_SCHEDULINGCAPS;
typedef DXGK_VIDSCHCAPS *PDXGK_SCHEDULINGCAPS;

typedef struct _DXGK_VIDMMCAPS
{
    union
    {
        struct
        {
            UINT OutOfOrderLock : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
            UINT DedicatedPagingEngine : 1;
            UINT PagingEngineCanSwizzle : 1;
            UINT SectionBackedPrimary   : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
            UINT CrossAdapterResource : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
            UINT VirtualAddressingSupported : 1;
            UINT GpuMmuSupported            : 1;
            UINT IoMmuSupported             : 1;
            UINT ReplicateGdiContent        : 1;
            UINT NonCpuVisiblePrimary       : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
            UINT ParavirtualizationSupported : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
            UINT IoMmuSecureModeSupported   : 1;
            UINT DisableSelfRefreshVRAMInS3 : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
            UINT IoMmuSecureModeRequired : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
            UINT MapAperture2Supported        : 1;
            UINT CrossAdapterResourceTexture  : 1;
            UINT CrossAdapterResourceScanout  : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
            UINT AlwaysPoweredVRAM : 1;
            UINT Reserved          : 14;
#else
            UINT Reserved          : 15;
#endif
#else
            UINT Reserved          : 18;
#endif
#else
            UINT Reserved          : 19;
#endif
#else
            UINT Reserved          : 21;
#endif
#else
            UINT Reserved          : 22;
#endif
#else
            UINT Reserved          : 27;
#endif
#else
            UINT Reserved          : 28;
#endif
#else
            UINT Reserved          : 31;
#endif
        };
        UINT Value;
    };
    UINT PagingNode;
} DXGK_VIDMMCAPS, *PDXGK_VIDMMCAPS;

typedef DXGK_VIDMMCAPS DXGK_MEMORYMANAGEMENTCAPS;
typedef DXGK_VIDMMCAPS *PDXGK_MEMORYMANAGEMENTCAPS;

C_ASSERT(sizeof(DXGK_GAMMARAMPCAPS) == 0x4);
C_ASSERT(sizeof(DXGK_PRESENTATIONCAPS) == 0x4);
C_ASSERT(sizeof(DXGK_FLIPCAPS) == 0x4);
C_ASSERT(sizeof(DXGK_VIDSCHCAPS) == 0x4);
C_ASSERT(sizeof(DXGK_VIDMMCAPS) == 0x8);

#define DXGK_MAX_ASYMETRICAL_PROCESSING_NODES 64

typedef struct _DXGK_GPUENGINETOPOLOGY
{
    UINT NbAsymetricProcessingNodes;
    UINT Reserved[DXGK_MAX_ASYMETRICAL_PROCESSING_NODES];
} DXGK_GPUENGINETOPOLOGY, *PDXGK_GPUENGINETOPOLOGY;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
typedef struct _DXGK_HWQUEUEDFLIP_CAPS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGK_HWQUEUEDFLIP_CAPS;
#endif


/* =========================================================================
 * DXGK_DRIVERCAPS (+ the deprecated _ADVSCH_ sub-structures it embeds)
 *
 * Capability record returned by
 * DxgkDdiQueryAdapterInfo(DXGKQAITYPE_DRIVERCAPS).
 *
 * Layout and capability-bit positions match the Windows SDK 10.0.26100
 * contract through WDDM 3.2, including the WDDM 2.4 and 2.9 tail fields.
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
     DXGKDDI_WDDMv1      = 0x1000,
     DXGKDDI_WDDMv1_2    = 0x1200,
     DXGKDDI_WDDMv1_3    = 0x1300,
     DXGKDDI_WDDMv2      = 0x2000,
     DXGKDDI_WDDMv2_1    = 0x2100,
     DXGKDDI_WDDMv2_1_5  = 0x2105,
     DXGKDDI_WDDMv2_1_6  = 0x2106,
     DXGKDDI_WDDMv2_2    = 0x2200,
     DXGKDDI_WDDMv2_3    = 0x2300,
     DXGKDDI_WDDMv2_4    = 0x2400,
     DXGKDDI_WDDMv2_5    = 0x2500,
     DXGKDDI_WDDMv2_6    = 0x2600,
     DXGKDDI_WDDMv2_7    = 0x2700,
     DXGKDDI_WDDMv2_8    = 0x2800,
     DXGKDDI_WDDMv2_9    = 0x2900,
     DXGKDDI_WDDMv3_0    = 0x3000,
     DXGKDDI_WDDMv3_1    = 0x3100,
     DXGKDDI_WDDMv3_2    = 0x3200,
     DXGKDDI_WDDM_LATEST = DXGKDDI_WDDMv3_2,
} DXGK_WDDMVERSION;

C_ASSERT(DXGKDDI_WDDMv3_1 == 0x3100);
C_ASSERT(DXGKDDI_WDDMv3_2 == 0x3200);
C_ASSERT(DXGKDDI_WDDM_LATEST == DXGKDDI_WDDMv3_2);

#define DXGKDDI_WDDMv1_ENUM DXGKDDI_WDDMv1
#define DXGKDDI_WDDMv1_2_ENUM DXGKDDI_WDDMv1_2
#define DXGKDDI_WDDMv1_3_ENUM DXGKDDI_WDDMv1_3
#define DXGKDDI_WDDMv2_ENUM DXGKDDI_WDDMv2
#define DXGKDDI_WDDMv2_1_ENUM DXGKDDI_WDDMv2_1
#define DXGKDDI_WDDMv2_2_ENUM DXGKDDI_WDDMv2_2
#define DXGKDDI_WDDMv2_3_ENUM DXGKDDI_WDDMv2_3

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
    union
    {
        DXGK_GAMMARAMPCAPS GammaRampCaps;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
        DXGK_COLORTRANSFORMCAPS ColorTransformCaps;
#endif
    };
    DXGK_PRESENTATIONCAPS   PresentationCaps;
    UINT                    MaxQueuedFlipOnVSync;
    DXGK_FLIPCAPS           FlipCaps;
    DXGK_VIDSCHCAPS         SchedulingCaps;
    DXGK_VIDMMCAPS          MemoryManagementCaps;
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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    union
    {
        struct
        {
            UINT SupportContextlessPresent : 1;
            UINT Detachable                : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
            UINT VirtualGpuOnly : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
            UINT ComputeOnly : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
            UINT IndependentVidPnVSyncControl : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)
            UINT NoHybridDiscreteDListDllSupport : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
            UINT DisplayableSupport : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
            UINT NoHybridDiscreteDListDllMuxSupport : 1;
            UINT CursorDoesNotSupportXorBlendWithMultiPlaneOverlay : 1;
            UINT Reserved : 23;
#else
            UINT Reserved : 25;
#endif
#else
            UINT Reserved : 26;
#endif
#else
            UINT Reserved : 27;
#endif
#else
            UINT Reserved : 28;
#endif
#else
            UINT Reserved : 29;
#endif
#else
            UINT Reserved : 30;
#endif
        };
        UINT Value;
    } MiscCaps;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    UINT MaxHwQueuedFlips;
    DXGK_HWQUEUEDFLIP_CAPS HwQueuedFlipCaps;
#endif
} DXGK_DRIVERCAPS, *PDXGK_DRIVERCAPS;

#if defined(_WIN64) && (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
C_ASSERT(sizeof(DXGK_DRIVERCAPS) == 0x250);
C_ASSERT(FIELD_OFFSET(DXGK_DRIVERCAPS, MiscCaps) == 0x240);
C_ASSERT(FIELD_OFFSET(DXGK_DRIVERCAPS, MaxHwQueuedFlips) == 0x244);
C_ASSERT(FIELD_OFFSET(DXGK_DRIVERCAPS, HwQueuedFlipCaps) == 0x248);
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
C_ASSERT(sizeof(DXGK_DRIVERCAPS) == 0x248);
C_ASSERT(FIELD_OFFSET(DXGK_DRIVERCAPS, MiscCaps) == 0x238);
C_ASSERT(FIELD_OFFSET(DXGK_DRIVERCAPS, MaxHwQueuedFlips) == 0x23C);
C_ASSERT(FIELD_OFFSET(DXGK_DRIVERCAPS, HwQueuedFlipCaps) == 0x240);
#endif


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
            UINT    PreservedDuringStandby              : 1;
            UINT    PreservedDuringHibernate            : 1;
            UINT    PartiallyPreservedDuringHibernate   : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
            UINT    DirectFlip                          : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
            UINT    Use64KBPages                        : 1;
            UINT    ReservedSysMem                      : 1;
            UINT    SupportsCpuHostAperture             : 1;
            UINT    SupportsCachedCpuHostAperture       : 1;
            UINT    ApplicationTarget                   : 1;
            UINT    VprSupported                        : 1;
            UINT    VprPreservedDuringStandby           : 1;
            UINT    EncryptedPagingSupported            : 1;
            UINT    LocalBudgetGroup                    : 1;
            UINT    NonLocalBudgetGroup                 : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
            UINT    PopulatedByReservedDDRByFirmware    : 1;
            UINT    Reserved                            : 10;
#else
            UINT    Reserved                            : 11;
#endif
#else
            UINT    Reserved                            : 21;
#endif
#else
            UINT    Reserved                            : 22;
#endif
        };
        UINT    Value;
    };
} DXGK_SEGMENTFLAGS, *PDXGK_SEGMENTFLAGS;

C_ASSERT(sizeof(DXGK_SEGMENTFLAGS) == sizeof(UINT));


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

typedef struct _DXGK_QUERYSEGMENTIN
{
    PHYSICAL_ADDRESS    AgpApertureBase;
    LARGE_INTEGER       AgpApertureSize;
    DXGK_SEGMENTFLAGS   AgpFlags;
} DXGK_QUERYSEGMENTIN, *PDXGK_QUERYSEGMENTIN;

C_ASSERT(sizeof(DXGK_QUERYSEGMENTIN) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGK_QUERYSEGMENTIN, AgpApertureBase) == 0x0);
C_ASSERT(FIELD_OFFSET(DXGK_QUERYSEGMENTIN, AgpApertureSize) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGK_QUERYSEGMENTIN, AgpFlags) == 0x10);

/*
 * DXGK_SEGMENTDESCRIPTOR3 / DXGK_QUERYSEGMENTOUT / DXGK_QUERYSEGMENTOUT3
 *
 * Layouts match the genuine WDK d3dkmddi.h (winsdk-10 10.0.16299) exactly.
 * Note DESCRIPTOR3 is NOT a field-reordered DESCRIPTOR: Flags moves to the
 * front and two SIZE_T fields are appended.  QUERYSEGMENT[3] is a two-pass
 * protocol: first call has pSegmentDescriptor == NULL and the miniport sets
 * NbSegment; the second call provides the descriptor array to fill.
 */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
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
#endif

typedef struct _DXGK_QUERYSEGMENTOUT
{
    UINT                        NbSegment;
    DXGK_SEGMENTDESCRIPTOR     *pSegmentDescriptor;
    UINT                        PagingBufferSegmentId;
    UINT                        PagingBufferSize;
    UINT                        PagingBufferPrivateDataSize;
} DXGK_QUERYSEGMENTOUT, *PDXGK_QUERYSEGMENTOUT;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
typedef struct _DXGK_CPUHOSTAPERTURE
{
    UINT64  PhysicalAddress;
    UINT32  SizeInPages;
} DXGK_CPUHOSTAPERTURE;

typedef struct _DXGK_QUERYSEGMENTIN4
{
    UINT                    PhysicalAdapterIndex;
} DXGK_QUERYSEGMENTIN4;

C_ASSERT(sizeof(DXGK_QUERYSEGMENTIN4) == 0x4);

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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    UINT                     NumUEFIFrameBufferRanges;
#endif
} DXGK_SEGMENTDESCRIPTOR4, *PDXGK_SEGMENTDESCRIPTOR4;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGK_SEGMENTDESCRIPTOR4, NumUEFIFrameBufferRanges) == 0x5C);
C_ASSERT(sizeof(DXGK_SEGMENTDESCRIPTOR4) == 0x60);
#else
C_ASSERT(FIELD_OFFSET(DXGK_SEGMENTDESCRIPTOR4, NumUEFIFrameBufferRanges) == 0x48);
C_ASSERT(sizeof(DXGK_SEGMENTDESCRIPTOR4) == 0x50);
#endif
#else
#ifdef _WIN64
C_ASSERT(sizeof(DXGK_SEGMENTDESCRIPTOR4) == 0x60);
#else
C_ASSERT(sizeof(DXGK_SEGMENTDESCRIPTOR4) == 0x48);
#endif
#endif

typedef struct _DXGK_QUERYSEGMENTOUT4
{
    UINT    NbSegment;
    BYTE   *pSegmentDescriptor;
    UINT    PagingBufferSegmentId;
    UINT    PagingBufferSize;
    UINT    PagingBufferPrivateDataSize;
    SIZE_T  SegmentDescriptorStride;
} DXGK_QUERYSEGMENTOUT4, *PDXGK_QUERYSEGMENTOUT4;

typedef struct _DXGK_MEMORYRANGE
{
    UINT64 SegmentOffset;
    UINT64 SizeInBytes;
} DXGK_MEMORYRANGE;

C_ASSERT(sizeof(DXGK_MEMORYRANGE) == 0x10);

typedef struct _DXGK_QUERYSEGMENTMEMORYSTATE
{
    WORD DriverSegmentId;
    WORD PhysicalAdapterIndex;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    union
    {
        UINT NumInvalidMemoryRanges;
        UINT NumUEFIFrameBufferRanges;
    };
#else
    UINT NumInvalidMemoryRanges;
#endif
    DXGK_MEMORYRANGE *pMemoryRanges;
} DXGK_QUERYSEGMENTMEMORYSTATE, DXGK_SEGMENTMEMORYSTATE;

typedef _In_ CONST DXGK_SEGMENTMEMORYSTATE *IN_CONST_PDXGK_SEGMENTMEMORYSTATE;

C_ASSERT(FIELD_OFFSET(DXGK_QUERYSEGMENTMEMORYSTATE, pMemoryRanges) == 0x8);
#ifdef _WIN64
C_ASSERT(sizeof(DXGK_QUERYSEGMENTMEMORYSTATE) == 0x10);
#else
C_ASSERT(sizeof(DXGK_QUERYSEGMENTMEMORYSTATE) == 0xC);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION_WDDM2_0 */
#endif /* DXGKDDI_INTERFACE_VERSION_WIN8 */


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

typedef struct _DXGK_PAGE_TABLE_LEVEL_DESC
{
    UINT PageTableIndexBitCount;
    UINT PageTableSegmentId;
    UINT PagingProcessPageTableSegmentId;
    UINT PageTableSizeInBytes;
    UINT PageTableAlignmentInBytes;
} DXGK_PAGE_TABLE_LEVEL_DESC;

C_ASSERT(sizeof(DXGK_PAGE_TABLE_LEVEL_DESC) == 0x14);

typedef struct _DXGK_QUERYGPUMMUCAPSIN
{
    UINT PhysicalAdapterIndex;
} DXGK_QUERYGPUMMUCAPSIN;

typedef struct _DXGK_QUERYPAGETABLELEVELDESCIN
{
    WORD LevelIndex;
    WORD PhysicalAdapterIndex;
} DXGK_QUERYPAGETABLELEVELDESCIN;

typedef struct _DXGK_QUERYHISTORYBUFFERPRECISIONIN
{
    UINT PhysicalAdapterIndex;
} DXGK_QUERYHISTORYBUFFERPRECISIONIN;

C_ASSERT(sizeof(DXGK_QUERYGPUMMUCAPSIN) == 0x4);
C_ASSERT(sizeof(DXGK_QUERYPAGETABLELEVELDESCIN) == 0x4);
C_ASSERT(sizeof(DXGK_QUERYHISTORYBUFFERPRECISIONIN) == 0x4);

typedef struct _DXGK_GPUMMUCAPS
{
    union
    {
        struct
        {
            UINT ReadOnlyMemorySupported                : 1;
            UINT NoExecuteMemorySupported               : 1;
            UINT ZeroInPteSupported                     : 1;
            UINT ExplicitPageTableInvalidation          : 1;
            UINT CacheCoherentMemorySupported           : 1;
            UINT PageTableUpdateRequireAddressSpaceIdle : 1;
            UINT LargePageSupported                     : 1;
            UINT DualPteSupported                       : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
            UINT AllowNonAlignedLargePageAddress        : 1;
            UINT SysMem64KBPageSupported                : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
            UINT InvalidTlbEntriesNotCached             : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
            UINT SysMemLargePageSupported               : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
            UINT CachedPageTables                       : 1;
            UINT Reserved                               : 19;
#else
            UINT Reserved                               : 20;
#endif
#else
            UINT Reserved                               : 21;
#endif
#else
            UINT Reserved                               : 22;
#endif
#else
            UINT Reserved                               : 24;
#endif // (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        };
        UINT        Value;
    };
    DXGK_PAGETABLEUPDATEMODE    PageTableUpdateMode;
    UINT                        VirtualAddressBitCount;
    UINT                        LeafPageTableSizeFor64KPagesInBytes;
    UINT                        PageTableLevelCount;
    struct
    {
        UINT SourcePageTableVaInTransfer  : 1;
        UINT Reserved                     : 31;
    } LegacyBehaviors;
} DXGK_GPUMMUCAPS;

C_ASSERT(sizeof(DXGK_GPUMMUCAPS) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGK_GPUMMUCAPS, PageTableUpdateMode) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGK_GPUMMUCAPS, VirtualAddressBitCount) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGK_GPUMMUCAPS, LeafPageTableSizeFor64KPagesInBytes) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGK_GPUMMUCAPS, PageTableLevelCount) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGK_GPUMMUCAPS, LegacyBehaviors) == 0x14);

typedef struct _DXGK_QUERYPHYSICALADAPTERCAPSIN
{
    UINT PhysicalAdapterIndex;
} DXGK_QUERYPHYSICALADAPTERCAPSIN;

typedef struct _DXGK_PHYSICALADAPTERFLAGS
{
    union
    {
        struct
        {
            UINT IoMmuSupported             : 1;
            UINT GpuMmuSupported            : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
            UINT MovePagingSupported        : 1;
            UINT VPRPagingContextRequired   : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
            UINT AllowHardwareProtectedNoVpr : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
            UINT VirtualCopyEngineSupported : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
            UINT GpuVaIommuRequired         : 1;
            UINT GpuVaIommuGlobalRequired   : 1;
            UINT GpuVaIommuCacheCoherent    : 1;
            UINT Reserved                   : 23;
#else
            UINT Reserved                   : 26;
#endif
#else
            UINT Reserved                   : 27;
#endif
#else
            UINT Reserved                   : 28;
#endif
#else
            UINT Reserved                   : 30;
#endif
        };
        UINT Value;
    };
} DXGK_PHYSICALADAPTERFLAGS;

typedef struct _DXGK_PHYSICALADAPTERCAPS
{
    WORD NumExecutionNodes;
    WORD PagingNodeIndex;
    HANDLE DxgkPhysicalAdapterHandle;
    DXGK_PHYSICALADAPTERFLAGS Flags;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    UINT VPRPagingNode;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
    UINT VirtualCopyNodeIndex;
#endif
} DXGK_PHYSICALADAPTERCAPS;

C_ASSERT(sizeof(DXGK_QUERYPHYSICALADAPTERCAPSIN) == 0x4);
C_ASSERT(sizeof(DXGK_PHYSICALADAPTERFLAGS) == 0x4);
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGK_PHYSICALADAPTERCAPS, DxgkPhysicalAdapterHandle) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGK_PHYSICALADAPTERCAPS, Flags) == 0x10);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
C_ASSERT(sizeof(DXGK_PHYSICALADAPTERCAPS) == 0x20);
#else
C_ASSERT(sizeof(DXGK_PHYSICALADAPTERCAPS) == 0x18);
#endif
#else
C_ASSERT(FIELD_OFFSET(DXGK_PHYSICALADAPTERCAPS, DxgkPhysicalAdapterHandle) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGK_PHYSICALADAPTERCAPS, Flags) == 0x8);
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
C_ASSERT(sizeof(DXGK_PHYSICALADAPTERCAPS) == 0x14);
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
C_ASSERT(sizeof(DXGK_PHYSICALADAPTERCAPS) == 0x10);
#else
C_ASSERT(sizeof(DXGK_PHYSICALADAPTERCAPS) == 0xC);
#endif
#endif

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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
    UINT NativeFence    : 1;
    UINT Reserved       : 27;
#else
    UINT Reserved       : 28;
#endif
} DXGK_UPDATEPAGETABLEFLAGS;

C_ASSERT(sizeof(DXGK_UPDATEPAGETABLEFLAGS) == 0x4);

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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
            UINT FlipStereo               : 1;
            UINT FlipStereoTemporaryMono  : 1;
            UINT FlipStereoPreferRight    : 1;
            UINT SharedPrimaryTransition  : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
            UINT IndependentFlipExclusive : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
            UINT MoveFlip                 : 1;
            UINT Reserved                 : 23;
#else
            UINT Reserved                 : 24;
#endif
#else
            UINT Reserved                 : 25;
#endif
#else
            UINT Reserved                 : 29;
#endif
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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)

typedef enum _DXGK_INTERRUPT_STATE
{
    DXGK_INTERRUPT_ENABLE = 0,
    DXGK_INTERRUPT_DISABLE = 1
} DXGK_INTERRUPT_STATE;

typedef enum _DXGK_CRTC_VSYNC_STATE
{
    DXGK_VSYNC_ENABLE = 0,
    DXGK_VSYNC_DISABLE_KEEP_PHASE = 1,
    DXGK_VSYNC_DISABLE_NO_PHASE = 2
} DXGK_CRTC_VSYNC_STATE;

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3 */

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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
            UINT    TestContext             : 1;
            UINT    Reserved                : 26;
#else
            UINT    Reserved                 : 27;
#endif
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

typedef _Inout_ DXGKARG_PRESENT *INOUT_PDXGKARG_PRESENT;

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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
typedef struct _DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO3
{
    DWORD LayerIndex;
    ULONG FirstFreeFlipQueueLogEntryIndex;
} DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO3;
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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
        struct
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
            UINT                           PhysicalAdapterMask;
            UINT                           MultiPlaneOverlayVsyncInfoCount;
            _Field_size_(MultiPlaneOverlayVsyncInfoCount)
            DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO3
                *pMultiPlaneOverlayVsyncInfo;
            ULONGLONG GpuFrequency;
            ULONGLONG GpuClockCounter;
        } CrtcVsyncWithMultiPlaneOverlay3;
#endif
        struct
        {
            UINT Reserved[16];
        } Reserved;
    };
    DXGKCB_NOTIFY_INTERRUPT_DATA_FLAGS Flags;
} DXGKARGCB_NOTIFY_INTERRUPT_DATA, *PDXGKARGCB_NOTIFY_INTERRUPT_DATA;


/*
 * DXGKARGCB_ALLOCATECONTIGUOUSMEMORY, DXGKARGCB_FREECONTIGUOUSMEMORY
 * and DXGKARGCB_MAPPHYSICALMEMORY
 *
 * These legacy helper structures are internal to dxgkrnl.sys and are
 * defined in dxgkrnl_private.h.  They are distinct from the public WDDM
 * 2.9 physical-memory-object callback arguments declared below.
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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
typedef struct _DXGK_DISPLAY_DRIVERCAPS_EXTENSION
{
    union
    {
        struct
        {
            UINT SecureDisplaySupport : 1;
            UINT VirtualModeSupport   : 1;
#if (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_1)
            UINT Reserved             : 29;
            UINT NonSpecificPrimarySupport : 1;
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
            UINT HdrFP16ScanoutSupport   : 1;
            UINT HdrARGB10ScanoutSupport : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
            UINT Hdr10MetadataSupport : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
            UINT VirtualRefreshRateSupport : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
            UINT SupportUsb4Targets : 1;
            UINT Reserved           : 25;
#else
            UINT Reserved           : 26;
#endif
#else
            UINT Reserved           : 27;
#endif
#else
            UINT Reserved           : 28;
#endif
#else
            UINT Reserved             : 30;
#endif
        };
        UINT Value;
    };
} DXGK_DISPLAY_DRIVERCAPS_EXTENSION, *PDXGK_DISPLAY_DRIVERCAPS_EXTENSION;

C_ASSERT(sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION) == sizeof(UINT));
#endif


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

typedef _In_ CONST HANDLE IN_CONST_HANDLE;
typedef _In_ CONST PVOID IN_CONST_PVOID;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)

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

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)

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

/* --- WDDM 2.0 power and video-protected-region callbacks --------------- */

typedef
    _Check_return_
    NTSTATUS
    APIENTRY
    DXGKDDI_POWERRUNTIMESETDEVICEHANDLE(
        IN_CONST_HANDLE DriverContext,
        _In_ HANDLE     PoDeviceHandle);

typedef struct DXGKARG_SETSTABLEPOWERSTATE
{
    BOOL Enabled;
} DXGKARG_SETSTABLEPOWERSTATE;

typedef _In_ CONST DXGKARG_SETSTABLEPOWERSTATE*
    IN_CONST_PDXGKARG_SETSTABLEPOWERSTATE;

typedef
    VOID
    APIENTRY
    DXGKDDI_SETSTABLEPOWERSTATE(
        IN_CONST_HANDLE                       hAdapter,
        IN_CONST_PDXGKARG_SETSTABLEPOWERSTATE pArgs);

typedef struct _DXGKARG_SETVIDEOPROTECTEDREGION
{
    UINT   PhysicalAdapterIndex;
    UINT   SegmentIndex;
    UINT   VprIndex;
    SIZE_T CurrentStartOffset;
    SIZE_T CurrentSize;
    SIZE_T NewStartOffset;
    SIZE_T NewSize;
} DXGKARG_SETVIDEOPROTECTEDREGION;

typedef _In_ CONST DXGKARG_SETVIDEOPROTECTEDREGION*
    IN_CONST_PDXGKARG_SETVIDEOPROTECTEDREGION;

typedef
    _Check_return_
    NTSTATUS
    APIENTRY
    DXGKDDI_SETVIDEOPROTECTEDREGION(
        IN_CONST_HANDLE                            hAdapter,
        IN_CONST_PDXGKARG_SETVIDEOPROTECTEDREGION pArgs);

typedef DXGKDDI_POWERRUNTIMESETDEVICEHANDLE *PDXGKDDI_POWERRUNTIMESETDEVICEHANDLE;
typedef DXGKDDI_SETSTABLEPOWERSTATE         *PDXGKDDI_SETSTABLEPOWERSTATE;
typedef DXGKDDI_SETVIDEOPROTECTEDREGION     *PDXGKDDI_SETVIDEOPROTECTEDREGION;

C_ASSERT(sizeof(DXGKARG_SETSTABLEPOWERSTATE) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETSTABLEPOWERSTATE, Enabled) == 0x0);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, PhysicalAdapterIndex) == 0x0);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, SegmentIndex) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, VprIndex) == 0x8);
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, CurrentStartOffset) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, CurrentSize) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, NewStartOffset) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, NewSize) == 0x28);
C_ASSERT(sizeof(DXGKARG_SETVIDEOPROTECTEDREGION) == 0x30);
#else
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, CurrentStartOffset) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, CurrentSize) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, NewStartOffset) == 0x14);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDEOPROTECTEDREGION, NewSize) == 0x18);
C_ASSERT(sizeof(DXGKARG_SETVIDEOPROTECTEDREGION) == 0x1C);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0 */

/* --- DxgkDdiCheckMultiPlaneOverlaySupport2 ---------------------------- */

/* =========================================================================
 * Multi-plane overlay (MPO) — layouts match the genuine WDK d3dkmddi.h
 * (winsdk-10 10.0.16299) verbatim for the WDDM1.3-era DDI set.
 * ========================================================================= */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)

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
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    RECT                                           ClipRect;
#endif
    D3DDDI_ROTATION                                Rotation;
    DXGK_MULTIPLANE_OVERLAY_BLEND                  Blend;
#if (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    UINT                                           NumFilters;
    VOID                                          *pFilters;
#endif
    DXGK_MULTIPLANE_OVERLAY_VIDEO_FRAME_FORMAT     VideoFrameFormat;
    DXGK_MULTIPLANE_OVERLAY_YCbCr_FLAGS            YCbCrFlags;
    DXGK_MULTIPLANE_OVERLAY_STEREO_FORMAT          StereoFormat;
    BOOL                                           StereoLeftViewFrame0;
    BOOL                                           StereoBaseViewFrame0;
    DXGK_MULTIPLANE_OVERLAY_STEREO_FLIP_MODE       StereoFlipMode;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY        StretchQuality;
#endif
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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
typedef struct _DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT
{
    UINT                                               PlaneCount;
    DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_PLANE       *pPlanes;
    BOOL                                               Supported;
    DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_RETURN_INFO  ReturnInfo;
} DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT, *PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT;

C_ASSERT(FIELD_OFFSET(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT, PlaneCount) == 0x0);
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT, pPlanes) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT, Supported) == 0x10);
C_ASSERT(sizeof(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT) == 0x18);
#else
C_ASSERT(FIELD_OFFSET(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT, pPlanes) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT, Supported) == 0x8);
C_ASSERT(sizeof(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT) == 0x10);
#endif
#endif

typedef struct _DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY
{
    UINT                             ContextCount;
    HANDLE                           Context[1+D3DDDI_MAX_BROADCAST_CONTEXT];
    DXGK_SETVIDPNSOURCEADDRESS_FLAGS Flags;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID   VidPnSourceId;
    UINT                             PlaneCount;
    DXGK_MULTIPLANE_OVERLAY_PLANE   *pPlanes;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    UINT                             Duration;
#endif
} DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY,
 *PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
typedef NTSTATUS
(APIENTRY *PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT)(
    IN_CONST_HANDLE                                 hAdapter,
    _Inout_ PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT  CheckMultiPlaneOverlaySupport);
#endif

typedef NTSTATUS
(APIENTRY *PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY)(
    IN_CONST_HANDLE                                               hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY *SetVidPnSourceAddressWithMpo);

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)

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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)

typedef struct _DXGK_SETVIDPNSOURCEADDRESS_INPUT_FLAGS
{
    union
    {
        struct
        {
            UINT FlipStereo              : 1;
            UINT FlipStereoTemporaryMono : 1;
            UINT FlipStereoPreferRight   : 1;
            UINT RetryAtLowerIrql        : 1;
            UINT Reserved                : 28;
        };
        UINT Value;
    };
} DXGK_SETVIDPNSOURCEADDRESS_INPUT_FLAGS;

typedef struct _DXGK_SETVIDPNSOURCEADDRESS_OUTPUT_FLAGS
{
    union
    {
        struct
        {
            UINT PrePresentNeeded : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
            UINT HwFlipQueueDrainNeeded     : 1;
            UINT HwFlipQueueDrainAllPlanes  : 1;
            UINT HwFlipQueueDrainAllSources : 1;
            UINT Reserved                   : 28;
#else
            UINT Reserved : 31;
#endif
        };
        UINT Value;
    };
} DXGK_SETVIDPNSOURCEADDRESS_OUTPUT_FLAGS;

typedef struct _DXGK_PLANE_SPECIFIC_INPUT_FLAGS
{
    union
    {
        struct
        {
            UINT Enabled                  : 1;
            UINT FlipImmediate            : 1;
            UINT FlipOnNextVSync          : 1;
            UINT SharedPrimaryTransition  : 1;
            UINT IndependentFlipExclusive : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
            UINT FlipImmediateNoTearing : 1;
            UINT Reserved               : 26;
#else
            UINT Reserved : 27;
#endif
        };
        UINT Value;
    };
} DXGK_PLANE_SPECIFIC_INPUT_FLAGS;

typedef struct _DXGK_PLANE_SPECIFIC_OUTPUT_FLAGS
{
    union
    {
        struct
        {
            UINT FlipConvertedToImmediate : 1;
            UINT PostPresentNeeded        : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
            UINT HsyncInterruptCompletion : 1;
            UINT Reserved                 : 29;
#else
            UINT Reserved : 30;
#endif
        };
        UINT Value;
    };
} DXGK_PLANE_SPECIFIC_OUTPUT_FLAGS;

typedef struct _DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3
{
    DXGK_MULTIPLANE_OVERLAY_FLAGS           Flags;
    RECT                                    SrcRect;
    RECT                                    DstRect;
    RECT                                    ClipRect;
    D3DDDI_ROTATION                         Rotation;
    DXGK_MULTIPLANE_OVERLAY_BLEND           Blend;
    D3DDDI_COLOR_SPACE_TYPE                 ColorSpaceType;
    DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY StretchQuality;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
    UINT                                    SDRWhiteLevel;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    UINT                                    DirtyRectCnt;
    _Field_size_opt_(DirtyRectCnt) CONST RECT *pDirtyRects;
#endif
} DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3;

typedef struct _DXGK_HDR_METADATA
{
    D3DDDI_HDR_METADATA_TYPE Type;
    UINT                     Size;
    PVOID                    pMetaData;
} DXGK_HDR_METADATA;

typedef struct _DXGK_PRIMARYCONTEXTDATA
{
    HANDLE hContext;
    HANDLE hAllocation;
    union
    {
        WORD SegmentId;
        WORD MmuId;
    };
    union
    {
        PHYSICAL_ADDRESS       SegmentAddress;
        D3DGPU_VIRTUAL_ADDRESS VirtualAddress;
    };
} DXGK_PRIMARYCONTEXTDATA;

typedef struct _DXGK_MULTIPLANE_OVERLAY_PLANE3
{
    UINT                              LayerIndex;
    ULONGLONG                         PresentId;
    DXGK_PLANE_SPECIFIC_INPUT_FLAGS   InputFlags;
    DXGK_PLANE_SPECIFIC_OUTPUT_FLAGS  OutputFlags;
    UINT                              MaxImmediateFlipLine;
    UINT                              ContextCount;
    _Field_size_(ContextCount) DXGK_PRIMARYCONTEXTDATA **ppContextData;
    UINT                              DriverPrivateDataSize;
    _Field_size_bytes_(DriverPrivateDataSize) PVOID pDriverPrivateData;
    DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3 PlaneAttributes;
} DXGK_MULTIPLANE_OVERLAY_PLANE3;

typedef struct _DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION_FLAGS
{
    union
    {
        struct
        {
            UINT VerticalFlip   : 1;
            UINT HorizontalFlip : 1;
            UINT Reserved       : 30;
        };
        UINT Value;
    };
} DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION_FLAGS;

typedef struct _DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION
{
    DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION_FLAGS Flags;
    RECT                                            SrcRect;
    RECT                                            DstRect;
    D3DDDI_ROTATION                                 Rotation;
} DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
#define DXGK_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3_DURATION_MAX ((UINT)-1)
#endif

typedef struct _DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID             VidPnSourceId;
    DXGK_SETVIDPNSOURCEADDRESS_INPUT_FLAGS     InputFlags;
    DXGK_SETVIDPNSOURCEADDRESS_OUTPUT_FLAGS    OutputFlags;
    UINT                                       PlaneCount;
    _Field_size_(PlaneCount) DXGK_MULTIPLANE_OVERLAY_PLANE3 **ppPlanes;
    DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION  *pPostComposition;
    UINT                                       Duration;
    DXGK_HDR_METADATA                         *pHDRMetaData;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
    UINT64                                     TargetFlipTime;
#endif
} DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3;

typedef _Inout_ DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 *IN_OUT_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3;

typedef _Check_return_ NTSTATUS APIENTRY DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3(IN_CONST_HANDLE hAdapter, IN_OUT_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 pSetVidPnSourceAddressWithMultiPlaneOverlay);
typedef DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 *PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3;

typedef struct _DXGKARG_POSTMULTIPLANEOVERLAYPRESENT
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
    UINT                           PhysicalAdapterMask;
    DWORD                          LayerIndex;
    ULONGLONG                      PresentID;
} DXGKARG_POSTMULTIPLANEOVERLAYPRESENT;

typedef _In_ CONST DXGKARG_POSTMULTIPLANEOVERLAYPRESENT *IN_CONST_PDXGKARG_POSTMULTIPLANEOVERLAYPRESENT;

typedef _Check_return_ NTSTATUS APIENTRY DXGKDDI_POSTMULTIPLANEOVERLAYPRESENT(IN_CONST_HANDLE hAdapter, IN_CONST_PDXGKARG_POSTMULTIPLANEOVERLAYPRESENT pPostPresent);
typedef DXGKDDI_POSTMULTIPLANEOVERLAYPRESENT *PDXGKDDI_POSTMULTIPLANEOVERLAYPRESENT;

typedef struct _DXGKARG_VALIDATEUPDATEALLOCPROPERTY
{
    HANDLE                           hAllocation;
    UINT                             SupportedSegmentSet;
    D3DDDI_SEGMENTPREFERENCE         PreferredSegment;
    D3DDDI_UPDATEALLOCPROPERTY_FLAGS Flags;
    union
    {
        struct
        {
            UINT SetAccessedPhysically  : 1;
            UINT SetSupportedSegmentSet : 1;
            UINT SetPreferredSegment    : 1;
            UINT Reserved               : 29;
        };
        UINT PropertyMaskValue;
    };
} DXGKARG_VALIDATEUPDATEALLOCPROPERTY;

typedef _In_ CONST DXGKARG_VALIDATEUPDATEALLOCPROPERTY *IN_CONST_PDXGKARG_VALIDATEUPDATEALLOCPROPERTY;

typedef _Check_return_ NTSTATUS APIENTRY DXGKDDI_VALIDATEUPDATEALLOCATIONPROPERTY(IN_CONST_HANDLE hAdapter, IN_CONST_PDXGKARG_VALIDATEUPDATEALLOCPROPERTY pValidateUpdateAllocProperty);
typedef DXGKDDI_VALIDATEUPDATEALLOCATIONPROPERTY *PDXGKDDI_VALIDATEUPDATEALLOCATIONPROPERTY;

typedef struct _DXGK_MULTIPLANE_OVERLAY_PLANE_WITH_SOURCE2
{
    HANDLE                              hAllocation;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID      VidPnSourceId;
    UINT                                LayerIndex;
    DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3 PlaneAttributes;
} DXGK_MULTIPLANE_OVERLAY_PLANE_WITH_SOURCE2;

typedef struct _DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION_WITH_SOURCE
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID          VidPnSourceId;
    DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION PostComposition;
} DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION_WITH_SOURCE;

typedef struct _DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3
{
    UINT PlaneCount;
    _Field_size_(PlaneCount) DXGK_MULTIPLANE_OVERLAY_PLANE_WITH_SOURCE2 **ppPlanes;
    UINT PostCompositionCount;
    _Field_size_(PostCompositionCount) DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION_WITH_SOURCE **ppPostComposition;
    BOOL Supported;
    DXGK_CHECK_MULTIPLANE_OVERLAY_SUPPORT_RETURN_INFO ReturnInfo;
} DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3;

typedef _Inout_ DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 *IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3;

typedef _Check_return_ NTSTATUS APIENTRY DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3(IN_CONST_HANDLE hAdapter, IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 pCheckMultiPlaneOverlaySupport);
typedef DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3 *PDXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3;

typedef union _DXGK_MODE_BEHAVIOR_FLAGS
{
    struct
    {
        UINT PrioritizeHDR       : 1;
        UINT ColorimetricControl : 1;
        UINT Reserved            : 30;
    };
    UINT Value;
} DXGK_MODE_BEHAVIOR_FLAGS;

typedef struct _DXGKARG_CONTROLMODEBEHAVIOR
{
    DXGK_MODE_BEHAVIOR_FLAGS Request;
    DXGK_MODE_BEHAVIOR_FLAGS Satisfied;
    DXGK_MODE_BEHAVIOR_FLAGS NotSatisfied;
} DXGKARG_CONTROLMODEBEHAVIOR;

typedef _Inout_ DXGKARG_CONTROLMODEBEHAVIOR *INOUT_PDXGKARG_CONTROLMODEBEHAVIOR;

typedef _Check_return_ NTSTATUS APIENTRY DXGKDDI_CONTROLMODEBEHAVIOR(IN_CONST_HANDLE hAdapter, INOUT_PDXGKARG_CONTROLMODEBEHAVIOR pControlModeBehaviorArg);
typedef DXGKDDI_CONTROLMODEBEHAVIOR *PDXGKDDI_CONTROLMODEBEHAVIOR;

typedef struct _DXGK_MONITORLINKINFO
{
    DXGK_MONITORLINKINFO_USAGEHINTS   UsageHints;
    DXGK_MONITORLINKINFO_CAPABILITIES Capabilities;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE DitheringSupport;
#endif
} DXGK_MONITORLINKINFO;

typedef struct _DXGKARG_UPDATEMONITORLINKINFO
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId;
    DXGK_MONITORLINKINFO           MonitorLinkInfo;
} DXGKARG_UPDATEMONITORLINKINFO;

typedef _Inout_ DXGKARG_UPDATEMONITORLINKINFO *INOUT_PDXGKARG_UPDATEMONITORLINKINFO;

typedef _Check_return_ NTSTATUS APIENTRY DXGKDDI_UPDATEMONITORLINKINFO(IN_CONST_HANDLE hAdapter, INOUT_PDXGKARG_UPDATEMONITORLINKINFO pUpdateMonitorLinkInfoArg);
typedef DXGKDDI_UPDATEMONITORLINKINFO *PDXGKDDI_UPDATEMONITORLINKINFO;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1) && (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_2)
C_ASSERT(sizeof(DXGK_SETVIDPNSOURCEADDRESS_INPUT_FLAGS) == 0x4);
C_ASSERT(sizeof(DXGK_SETVIDPNSOURCEADDRESS_OUTPUT_FLAGS) == 0x4);
C_ASSERT(sizeof(DXGK_PLANE_SPECIFIC_INPUT_FLAGS) == 0x4);
C_ASSERT(sizeof(DXGK_PLANE_SPECIFIC_OUTPUT_FLAGS) == 0x4);
C_ASSERT(sizeof(DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3) == 0x44);
C_ASSERT(FIELD_OFFSET(DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3, ColorSpaceType) == 0x3C);
C_ASSERT(sizeof(DXGKARG_POSTMULTIPLANEOVERLAYPRESENT) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARG_POSTMULTIPLANEOVERLAYPRESENT, PresentID) == 0x10);
C_ASSERT(sizeof(DXGKARG_CONTROLMODEBEHAVIOR) == 0xC);
C_ASSERT(sizeof(DXGKARG_UPDATEMONITORLINKINFO) == 0xC);
#ifdef _WIN64
C_ASSERT(sizeof(DXGK_PRIMARYCONTEXTDATA) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGK_PRIMARYCONTEXTDATA, SegmentAddress) == 0x18);
C_ASSERT(sizeof(DXGK_MULTIPLANE_OVERLAY_PLANE3) == 0x80);
C_ASSERT(FIELD_OFFSET(DXGK_MULTIPLANE_OVERLAY_PLANE3, ppContextData) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGK_MULTIPLANE_OVERLAY_PLANE3, PlaneAttributes) == 0x38);
C_ASSERT(sizeof(DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3) == 0x30);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3, ppPlanes) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3, pHDRMetaData) == 0x28);
C_ASSERT(sizeof(DXGKARG_VALIDATEUPDATEALLOCPROPERTY) == 0x18);
C_ASSERT(sizeof(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3) == 0x28);
C_ASSERT(FIELD_OFFSET(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3, ppPostComposition) == 0x18);
#else
C_ASSERT(sizeof(DXGK_PRIMARYCONTEXTDATA) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGK_PRIMARYCONTEXTDATA, SegmentAddress) == 0x10);
C_ASSERT(sizeof(DXGK_MULTIPLANE_OVERLAY_PLANE3) == 0x70);
C_ASSERT(FIELD_OFFSET(DXGK_MULTIPLANE_OVERLAY_PLANE3, ppContextData) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGK_MULTIPLANE_OVERLAY_PLANE3, PlaneAttributes) == 0x2C);
C_ASSERT(sizeof(DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3, ppPlanes) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3, pHDRMetaData) == 0x1C);
C_ASSERT(sizeof(DXGKARG_VALIDATEUPDATEALLOCPROPERTY) == 0x14);
C_ASSERT(sizeof(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3, ppPostComposition) == 0xC);
#endif
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)

/*
 * WDDM 2.2 hardware scheduling and display contracts.
 * Layout and signatures match the Windows 10 16299 and Windows 11 26100 WDKs.
 */

typedef struct _DXGK_HWCONTEXT_CAPS
{
    union
    {
        struct
        {
            UINT UseIoMmu : 1;
            UINT Reserved : 31;
        };
        UINT Value;
    };
} DXGK_HWCONTEXT_CAPS;

typedef struct _DXGKARG_CREATEHWCONTEXT
{
    HANDLE                      hHwContext;
    UINT                        NodeOrdinal;
    UINT                        EngineAffinity;
    D3DDDI_CREATEHWCONTEXTFLAGS Flags;
    UINT                        PrivateDriverDataSize;
    _Inout_
    _Field_size_bytes_(PrivateDriverDataSize)
    PVOID                       pPrivateDriverData;
    DXGK_HWCONTEXT_CAPS         ContextCaps;
} DXGKARG_CREATEHWCONTEXT;

typedef _Inout_ DXGKARG_CREATEHWCONTEXT *INOUT_PDXGKARG_CREATEHWCONTEXT;

typedef struct _DXGKARG_CREATEHWQUEUE
{
    HANDLE                     hHwQueue;
    D3DDDI_CREATEHWQUEUEFLAGS  Flags;
    UINT                       PrivateDriverDataSize;
    _Inout_
    _Field_size_bytes_(PrivateDriverDataSize)
    PVOID                      pPrivateDriverData;
    D3DKMT_HANDLE              hHwQueueProgressFence;
    PVOID                      HwQueueProgressFenceCPUVirtualAddress;
    D3DGPU_VIRTUAL_ADDRESS     HwQueueProgressFenceGPUVirtualAddress;
} DXGKARG_CREATEHWQUEUE;

typedef _Inout_ DXGKARG_CREATEHWQUEUE *INOUT_PDXGKARG_CREATEHWQUEUE;

typedef struct _DXGKARG_SUBMITCOMMANDTOHWQUEUE
{
    HANDLE                     hHwQueue;
    UINT64                     HwQueueProgressFenceId;
    D3DGPU_VIRTUAL_ADDRESS     DmaBufferVirtualAddress;
    UINT                       DmaBufferSize;
    UINT                       DmaBufferPrivateDataSize;
    _Field_size_bytes_(DmaBufferPrivateDataSize) PVOID pDmaBufferPrivateData;
    DXGK_SUBMITCOMMANDFLAGS    Flags;
    D3DGPU_VIRTUAL_ADDRESS     HwQueueProgressFenceGpuVa;
    PVOID                      HwQueueProgressFenceCpuVa;
} DXGKARG_SUBMITCOMMANDTOHWQUEUE;

typedef _In_ CONST DXGKARG_SUBMITCOMMANDTOHWQUEUE
    *IN_CONST_PDXGKARG_SUBMITCOMMANDTOHWQUEUE;

typedef struct _DXGKARG_SWITCHTOHWCONTEXTLIST
{
    HANDLE hHwContextFirst;
    HANDLE hHwContextSecond;
    UINT   NodeOrdinal;
    UINT   EngineOrdinal;
} DXGKARG_SWITCHTOHWCONTEXTLIST;

typedef _In_ CONST DXGKARG_SWITCHTOHWCONTEXTLIST
    *IN_CONST_PDXGKARG_SWITCHTOHWCONTEXTLIST;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
typedef struct _DXGK_UPDATEHWCONTEXTSTATE_FLAGS
{
    union
    {
        struct
        {
            UINT Suspended                   : 1;
            UINT InterruptOnSwitchCompletion : 1;
            UINT Reserved                    : 30;
        };
        UINT Value;
    };
} DXGK_UPDATEHWCONTEXTSTATE_FLAGS;

typedef struct _DXGKARG_UPDATEHWCONTEXTSTATE
{
    HANDLE                          hHwContext;
    UINT64                          ContextSwitchFence;
    UINT                            Priority;
    DXGK_UPDATEHWCONTEXTSTATE_FLAGS Flags;
} DXGKARG_UPDATEHWCONTEXTSTATE;

typedef _In_ CONST DXGKARG_UPDATEHWCONTEXTSTATE
    *IN_CONST_PDXGKARG_UPDATEHWCONTEXTSTATE;
#endif

typedef struct _DXGKARG_RESETHWENGINE
{
    UINT NodeOrdinal;
    UINT EngineOrdinal;
} DXGKARG_RESETHWENGINE;

typedef _Inout_ DXGKARG_RESETHWENGINE *INOUT_PDXGKARG_RESETHWENGINE;

typedef struct _DXGKARG_CREATEPERIODICFRAMENOTIFICATION
{
    HANDLE                         hAdapter;
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
    UINT64                         Time;
    UINT                           NotificationID;
    HANDLE                         hNotification;
} DXGKARG_CREATEPERIODICFRAMENOTIFICATION;

typedef _Inout_ DXGKARG_CREATEPERIODICFRAMENOTIFICATION
    *INOUT_PDXGKARG_CREATEPERIODICFRAMENOTIFICATION;

typedef struct _DXGKARG_DESTROYPERIODICFRAMENOTIFICATION
{
    HANDLE hNotification;
    HANDLE hAdapter;
} DXGKARG_DESTROYPERIODICFRAMENOTIFICATION;

typedef _In_ CONST DXGKARG_DESTROYPERIODICFRAMENOTIFICATION
    *IN_CONST_PDXGKARG_DESTROYPERIODICFRAMENOTIFICATION;

typedef struct _DXGK_MULTIPLANEOVERLAYCAPS
{
    union
    {
        struct
        {
            UINT Rotation                       : 1;
            UINT RotationWithoutIndependentFlip : 1;
            UINT VerticalFlip                   : 1;
            UINT HorizontalFlip                 : 1;
            UINT StretchRGB                     : 1;
            UINT StretchYUV                     : 1;
            UINT BilinearFilter                 : 1;
            UINT HighFilter                     : 1;
            UINT Shared                         : 1;
            UINT Immediate                      : 1;
            UINT Plane0ForVirtualModeOnly       : 1;
            UINT Reserved                       : 21;
        };
        UINT Value;
    };
} DXGK_MULTIPLANEOVERLAYCAPS;

typedef struct _DXGKARG_GETMULTIPLANEOVERLAYCAPS
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT                           MaxPlanes;
    UINT                           MaxRGBPlanes;
    UINT                           MaxYUVPlanes;
    DXGK_MULTIPLANEOVERLAYCAPS     OverlayCaps;
    float                          MaxStretchFactor;
    float                          MaxShrinkFactor;
} DXGKARG_GETMULTIPLANEOVERLAYCAPS;

typedef _Inout_ DXGKARG_GETMULTIPLANEOVERLAYCAPS
    *IN_OUT_PDXGKARG_GETMULTIPLANEOVERLAYCAPS;

typedef struct _DXGKARG_GETPOSTCOMPOSITIONCAPS
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    float                          MaxStretchFactor;
    float                          MaxShrinkFactor;
} DXGKARG_GETPOSTCOMPOSITIONCAPS;

typedef _Inout_ DXGKARG_GETPOSTCOMPOSITIONCAPS
    *IN_OUT_PDXGKARG_GETPOSTCOMPOSITIONCAPS;

typedef union _DXGK_STANDARD_COLORIMETRY_FLAGS
{
    struct
    {
        UINT BT2020YCC : 1;
        UINT BT2020RGB : 1;
        UINT ST2084    : 1;
        UINT Reserved  : 29;
    };
    ULONG Value;
} DXGK_STANDARD_COLORIMETRY_FLAGS, *PDXGK_STANDARD_COLORIMETRY_FLAGS;

typedef struct _DXGK_COLORIMETRY
{
    D3DKMDT_2DOFFSET                   RedPoint;
    D3DKMDT_2DOFFSET                   GreenPoint;
    D3DKMDT_2DOFFSET                   BluePoint;
    D3DKMDT_2DOFFSET                   WhitePoint;
    ULONG                              MinLuminance;
    ULONG                              MaxLuminance;
    ULONG                              MaxFullFrameLuminance;
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE FormatBitDepths;
    DXGK_STANDARD_COLORIMETRY_FLAGS    StandardColorimetryFlags;
} DXGK_COLORIMETRY, *PDXGK_COLORIMETRY;

#if defined(__cplusplus) && !defined(SORTPP_PASS)
typedef enum _DXGK_CONNECTION_STATUS : UINT
{
    ConnectionStatusUninitialized = 0,
    TargetStatusDisconnected      = 4,
    TargetStatusConnected         = 5,
    TargetStatusJoined            = 6,
    MonitorStatusDisconnected     = 8,
    MonitorStatusUnknown          = 9,
    MonitorStatusConnected        = 10,
    LinkConfigurationStarted      = 12,
    LinkConfigurationFailed       = 13,
    LinkConfigurationSucceeded    = 14
} DXGK_CONNECTION_STATUS, *PDXGK_CONNECTION_STATUS;
#else
typedef UINT DXGK_CONNECTION_STATUS;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
typedef struct _DXGK_CONNECTION_MONITOR_CONNECT_FLAGS
{
    union
    {
        struct
        {
            UINT Usb4DisplayPortMonitor : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
            UINT DisplayMuxConnectionChange : 1;
            UINT Reserved                   : 30;
#else
            UINT Reserved                   : 31;
#endif
        };
        UINT Value;
    };
} DXGK_CONNECTION_MONITOR_CONNECT_FLAGS;

typedef struct _DXGK_CONNECTION_USB4_INFO
{
    UINT Dpcd_DP_IN_Adapter_Number;
    UINT Dpcd_USB4_Driver_ID;
    BYTE Dpcd_USB4_ROUTER_TOPOLOGY_ID[5];
} DXGK_CONNECTION_USB4_INFO, *PDXGK_CONNECTION_USB4_INFO;
#endif

typedef struct _DXGK_CONNECTION_CHANGE
{
    ULONGLONG                      ConnectionChangeId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId         : 24;
    DXGK_CONNECTION_STATUS         ConnectionStatus : 4;
    UINT                           Reserved         : 4;
    union
    {
        struct
        {
            D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY LinkTargetType;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
            DXGK_CONNECTION_MONITOR_CONNECT_FLAGS MonitorConnectFlags;
#endif
        } MonitorConnect;
        struct
        {
            D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY BaseTargetType;
            D3DDDI_VIDEO_PRESENT_TARGET_ID  NewTargetId;
        } TargetConnect;
        struct
        {
            D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY BaseTargetType;
            D3DDDI_VIDEO_PRESENT_TARGET_ID  NewTargetId;
        } TargetJoin;
    };
} DXGK_CONNECTION_CHANGE, *PDXGK_CONNECTION_CHANGE;

typedef struct _DXGK_SET_TIMING_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved;
        };
        UINT Value;
    };
} DXGK_SET_TIMING_FLAGS;

typedef struct _DXGK_SET_TIMING_RESULTS
{
    union
    {
        struct
        {
            UINT ConnectionStatusChanges : 1;
            UINT Reserved                : 31;
        };
        UINT Value;
    };
} DXGK_SET_TIMING_RESULTS, *PDXGK_SET_TIMING_RESULTS;

#if defined(__cplusplus) && !defined(SORTPP_PASS)
typedef enum _DXGK_PATH_UPDATE : UINT
{
    DXGK_PATH_UPDATE_UNMODIFIED = 0,
    DXGK_PATH_UPDATE_ADDED      = 1,
    DXGK_PATH_UPDATE_MODIFIED   = 2,
    DXGK_PATH_UPDATE_REMOVED    = 3
} DXGK_PATH_UPDATE;

typedef enum _DXGK_SYNC_LOCK_STYLE : UINT
{
    DXGK_SYNC_LOCK_STYLE_NONE      = 0,
    DXGK_SYNC_LOCK_STYLE_IDENTICAL = 1
} DXGK_SYNC_LOCK_STYLE;

typedef enum _DXGK_GLITCH_CAUSE : UINT8
{
    DXGK_GLITCH_CAUSE_DRIVER_ERROR        = 0,
    DXGK_GLITCH_CAUSE_TIMING_CHANGE       = 1,
    DXGK_GLITCH_CAUSE_PIPELINE_CHANGE     = 2,
    DXGK_GLITCH_CAUSE_MEMORY_TIMING       = 3,
    DXGK_GLITCH_CAUSE_ENCODER_RECONFIG    = 4,
    DXGK_GLITCH_CAUSE_MODIFIED_WIRE_USAGE = 5,
    DXGK_GLITCH_CAUSE_METADATA_CHANGE     = 6,
    DXGK_GLITCH_CAUSE_NONE                = 255
} DXGK_GLITCH_CAUSE;

typedef enum _DXGK_GLITCH_EFFECT : UINT8
{
    DXGK_GLITCH_EFFECT_SYNC_LOSS        = 0,
    DXGK_GLITCH_EFFECT_GARBAGE_CONTENT  = 1,
    DXGK_GLITCH_EFFECT_STALE_CONTENT    = 2,
    DXGK_GLITCH_EFFECT_BLACK_CONTENT    = 3,
    DXGK_GLITCH_EFFECT_DEGRADED_CONTENT = 4,
    DXGK_GLITCH_EFFECT_SEAMLESS         = 255
} DXGK_GLITCH_EFFECT;

typedef enum _DXGK_GLITCH_DURATION : UINT8
{
    DXGK_GLITCH_DURATION_INDEFINITE  = 0,
    DXGK_GLITCH_DURATION_MULTI_FRAME = 1,
    DXGK_GLITCH_DURATION_SINGLE_FRAME = 2,
    DXGK_GLITCH_DURATION_MULTI_LINE  = 3,
    DXGK_GLITCH_DURATION_SINGLE_LINE = 4,
    DXGK_GLITCH_DURATION_NONE        = 255
} DXGK_GLITCH_DURATION;
#else
typedef UINT  DXGK_PATH_UPDATE;
typedef UINT  DXGK_SYNC_LOCK_STYLE;
typedef UINT8 DXGK_GLITCH_CAUSE;
typedef UINT8 DXGK_GLITCH_EFFECT;
typedef UINT8 DXGK_GLITCH_DURATION;
#endif

typedef struct _DXGK_SET_TIMING_PATH_INFO
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
    union
    {
        D3DDDI_COLOR_SPACE_TYPE             OutputColorSpace;
        D3DDDI_OUTPUT_WIRE_COLOR_SPACE_TYPE OutputWireColorSpace;
    };
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE SelectedWireFormat;
    union
    {
        struct
        {
            DXGK_PATH_UPDATE VidPnPathUpdates   : 2;
            UINT             Active             : 1;
            UINT             IgnoreConnectivity : 1;
            UINT             PreserveInherited  : 1;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
            UINT                 SyncLockGroup : 3;
            DXGK_SYNC_LOCK_STYLE SyncLockStyle : 4;
            UINT                 Reserved      : 20;
#else
            UINT Reserved : 27;
#endif
        } Input;
        UINT InputFlags;
    };
    union
    {
        struct
        {
            UINT RecheckMPO : 1;
            UINT Reserved   : 31;
        } Output;
        UINT OutputFlags;
    };
    DXGK_CONNECTION_CHANGE TargetState;
    union
    {
        struct
        {
            DXGK_GLITCH_CAUSE    GlitchCause;
            DXGK_GLITCH_EFFECT   GlitchEffect;
            DXGK_GLITCH_DURATION GlitchDuration;
            UINT8                Reserved;
        };
        UINT DiagnosticInfo;
    };
} DXGK_SET_TIMING_PATH_INFO;

typedef struct _DXGKARG_SETTIMINGSFROMVIDPN
{
    D3DKMDT_HVIDPN              hFunctionalVidPn;
    DXGK_SET_TIMING_FLAGS       SetFlags;
    PDXGK_SET_TIMING_RESULTS    pResultsFlags;
    UINT                        PathCount;
    _Field_size_(PathCount) DXGK_SET_TIMING_PATH_INFO *pSetTimingPathInfo;
} DXGKARG_SETTIMINGSFROMVIDPN;

typedef _Inout_ DXGKARG_SETTIMINGSFROMVIDPN *IN_OUT_PDXGKARG_SETTIMINGSFROMVIDPN;

typedef struct _DXGKARG_SETTARGETGAMMA
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    D3DKMDT_GAMMA_RAMP             GammaRamp;
} DXGKARG_SETTARGETGAMMA;

typedef _In_ CONST DXGKARG_SETTARGETGAMMA *IN_CONST_PDXGKARG_SETTARGETGAMMA;

typedef struct _DXGKARG_SETTARGETCONTENTTYPE
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    D3DKMDT_VIDPN_PRESENT_PATH_CONTENT ContentType;
} DXGKARG_SETTARGETCONTENTTYPE;

typedef _In_ CONST DXGKARG_SETTARGETCONTENTTYPE
    *IN_CONST_PDXGKARG_SETTARGETCONTENTTYPE;

typedef struct _DXGKARG_SETTARGETANALOGCOPYPROTECTION
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID                TargetId;
    D3DKMDT_VIDPN_PRESENT_PATH_COPYPROTECTION_TYPE CopyProtectionType;
    UINT                                           APSTriggerBits;
    D3DKMDT_VIDPN_PRESENT_PATH_COPYPROTECTION_SUPPORT CopyProtectionSupport;
} DXGKARG_SETTARGETANALOGCOPYPROTECTION;

typedef _In_ CONST DXGKARG_SETTARGETANALOGCOPYPROTECTION
    *IN_CONST_PDXGKARG_SETTARGETANALOGCOPYPROTECTION;

#if defined(__cplusplus) && !defined(SORTPP_PASS)
typedef enum _DXGK_DISPLAYDETECTCONTROLTYPE : UINT
{
    DXGK_DDCT_UNINITIALIZED = 0,
    DXGK_DDCT_POLLONE       = 1,
    DXGK_DDCT_POLLALL       = 2,
    DXGK_DDCT_ENABLEHPD     = 3,
    DXGK_DDCT_DISABLEHPD    = 4
} DXGK_DISPLAYDETECTCONTROLTYPE;
#else
typedef UINT DXGK_DISPLAYDETECTCONTROLTYPE;
#endif

typedef struct _DXGKARG_DISPLAYDETECTCONTROL
{
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId           : 24;
    DXGK_DISPLAYDETECTCONTROLTYPE  Type               : 4;
    UINT                           NonDestructiveOnly : 1;
    UINT                           Reserved           : 3;
} DXGKARG_DISPLAYDETECTCONTROL;

typedef _In_ CONST DXGKARG_DISPLAYDETECTCONTROL
    *IN_CONST_PDXGKARG_DISPLAYDETECTCONTROL;

typedef struct _DXGKARG_QUERYCONNECTIONCHANGE
{
    DXGK_CONNECTION_CHANGE ConnectionChange;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
    DXGK_CONNECTION_USB4_INFO Usb4MonitorInfo;
#endif
} DXGKARG_QUERYCONNECTIONCHANGE;

typedef _In_ DXGKARG_QUERYCONNECTIONCHANGE *IN_PDXGKARG_QUERYCONNECTIONCHANGE;

typedef struct _DXGK_INHERITED_TIMING_INFO
{
    union
    {
        D3DDDI_COLOR_SPACE_TYPE             OutputColorSpace;
        D3DDDI_OUTPUT_WIRE_COLOR_SPACE_TYPE OutputWireColorSpace;
    };
    D3DKMDT_WIRE_FORMAT_AND_PREFERENCE SelectedWireFormat;
    union
    {
        struct
        {
            DXGK_GLITCH_CAUSE    GlitchCause;
            DXGK_GLITCH_EFFECT   GlitchEffect;
            DXGK_GLITCH_DURATION GlitchDuration;
            UINT8                Reserved;
        };
        UINT DiagnosticInfo;
    };
} DXGK_INHERITED_TIMING_INFO, *PDXGK_INHERITED_TIMING_INFO;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
typedef struct _DXGKARG_CREATEPROTECTEDSESSION
{
    HANDLE hProtectedSession;
    PVOID  pPrivateDriverData;
    UINT   PrivateDriverDataSize;
} DXGKARG_CREATEPROTECTEDSESSION;

typedef _Inout_ DXGKARG_CREATEPROTECTEDSESSION
    *INOUT_PDXGKARG_CREATEPROTECTEDSESSION;

typedef enum _DXGK_PROTECTED_SESSION_STATUS
{
    DXGK_PROTECTED_SESSION_STATUS_OK      = 0,
    DXGK_PROTECTED_SESSION_STATUS_INVALID = 1
} DXGK_PROTECTED_SESSION_STATUS;

typedef struct _DXGKARGCB_PROTECTEDSESSIONSTATUS
{
    HANDLE                        hProtectedSession;
    DXGK_PROTECTED_SESSION_STATUS Status;
} DXGKARGCB_PROTECTEDSESSIONSTATUS;

typedef _In_ CONST DXGKARGCB_PROTECTEDSESSIONSTATUS
    *IN_CONST_PDXGKARGCB_PROTECTEDSESSIONSTATUS;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_SETPROTECTEDSESSIONSTATUS)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_SETPROTECTEDSESSIONSTATUS)(
    IN_CONST_PDXGKARGCB_PROTECTEDSESSIONSTATUS pProtectedSessionStatus);
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
typedef enum _DXGK_MEMORY_CACHING_TYPE
{
    DXGK_MEMORY_CACHING_TYPE_NON_CACHED,
    DXGK_MEMORY_CACHING_TYPE_CACHED,
    DXGK_MEMORY_CACHING_TYPE_WRITE_COMBINED
} DXGK_MEMORY_CACHING_TYPE;
#endif

typedef struct _DXGK_PRE_START_INFO
{
    union
    {
        struct
        {
            UINT ReservedIn;
        };
        UINT Input;
    };
    union
    {
        struct
        {
            UINT SupportPreserveBootDisplay                    : 1;
            UINT IsUEFIFrameBufferCpuAccessibleDuringStartup    : 1;
            UINT ReservedOut                                   : 30;
        };
        UINT Output;
    };
} DXGK_PRE_START_INFO, *PDXGK_PRE_START_INFO;

typedef _Inout_ PDXGK_PRE_START_INFO IN_OUT_PDXGK_PRE_START_INFO;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CREATEHWCONTEXT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_CREATEHWCONTEXT(
    IN_CONST_HANDLE hDevice,
    INOUT_PDXGKARG_CREATEHWCONTEXT pCreateContext);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DESTROYHWCONTEXT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_DESTROYHWCONTEXT(
    IN_CONST_HANDLE hHwContext);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CREATEHWQUEUE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_CREATEHWQUEUE(
    IN_CONST_HANDLE hHwContext,
    INOUT_PDXGKARG_CREATEHWQUEUE pCreateHwQueue);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DESTROYHWQUEUE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_DESTROYHWQUEUE(
    IN_CONST_HANDLE hHwQueue);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SUBMITCOMMANDTOHWQUEUE)
    _IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_SUBMITCOMMANDTOHWQUEUE(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDTOHWQUEUE pSubmitCommandToHwQueue);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SWITCHTOHWCONTEXTLIST)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_SWITCHTOHWCONTEXTLIST(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SWITCHTOHWCONTEXTLIST pHwContextList);

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_UPDATEHWCONTEXTSTATE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_UPDATEHWCONTEXTSTATE(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_UPDATEHWCONTEXTSTATE pUpdateHwContextState);
#endif

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_RESETHWENGINE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_RESETHWENGINE(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_RESETHWENGINE pResetHwEngine);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKARG_CREATEPERIODICFRAMENOTIFICATION)
    _IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_CREATEPERIODICFRAMENOTIFICATION(
    INOUT_PDXGKARG_CREATEPERIODICFRAMENOTIFICATION pCreatePeriodicFrameNotification);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKARG_DESTROYPERIODICFRAMENOTIFICATION)
    _IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_DESTROYPERIODICFRAMENOTIFICATION(
    IN_CONST_PDXGKARG_DESTROYPERIODICFRAMENOTIFICATION pDestroyPeriodicFrameNotification);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETTIMINGSFROMVIDPN)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_SETTIMINGSFROMVIDPN(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_SETTIMINGSFROMVIDPN pSetTimings);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETTARGETGAMMA)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_SETTARGETGAMMA(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETTARGETGAMMA pSetTargetGammaArg);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETTARGETCONTENTTYPE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_SETTARGETCONTENTTYPE(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETTARGETCONTENTTYPE pSetTargetContentTypeArg);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETTARGETANALOGCOPYPROTECTION)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_SETTARGETANALOGCOPYPROTECTION(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETTARGETANALOGCOPYPROTECTION pSetTargetAnalogCopyProtectionArg);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY(
    IN_CONST_HANDLE hAdapter,
    IN D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    IN DXGK_COLORIMETRY AdjustedColorimetry);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DISPLAYDETECTCONTROL)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_DISPLAYDETECTCONTROL(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_DISPLAYDETECTCONTROL pDisplayDetectControl);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_QUERYCONNECTIONCHANGE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_QUERYCONNECTIONCHANGE(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_QUERYCONNECTIONCHANGE pQueryConnectionChange);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_EXCHANGEPRESTARTINFO)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
DXGKDDI_EXCHANGEPRESTARTINFO(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGK_PRE_START_INFO pPreStartInfo);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_GETMULTIPLANEOVERLAYCAPS)
    _IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_GETMULTIPLANEOVERLAYCAPS(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_GETMULTIPLANEOVERLAYCAPS pGetMultiPlaneOverlayCaps);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_GETPOSTCOMPOSITIONCAPS)
    _IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_GETPOSTCOMPOSITIONCAPS(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_GETPOSTCOMPOSITIONCAPS pGetPostCompositionCaps);

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CREATEPROTECTEDSESSION)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_CREATEPROTECTEDSESSION(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEPROTECTEDSESSION pCreateProtectedSession);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DESTROYPROTECTEDSESSION)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS APIENTRY
DXGKDDI_DESTROYPROTECTEDSESSION(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hProtectedSession);
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)

typedef enum _DXGK_SCHEDULING_LOG_OPERATION
{
    DXGK_SCHEDULING_LOG_OPERATION_CONTEXT_STATE_CHANGE = 0
} DXGK_SCHEDULING_LOG_OPERATION;

typedef enum _DXGK_SCHEDULING_LOG_CONTEXT_STATE
{
    DXGK_SCHEDULING_LOG_CONTEXT_STATE_IDLE = 0,
    DXGK_SCHEDULING_LOG_CONTEXT_STATE_RUNNING = 1,
    DXGK_SCHEDULING_LOG_CONTEXT_STATE_READY = 2,
    DXGK_SCHEDULING_LOG_CONTEXT_STATE_READY_STANDBY = 3,
} DXGK_SCHEDULING_LOG_CONTEXT_STATE;

typedef struct _DXGK_SCHEDULING_LOG_CONTEXT_STATE_CHANGE
{
    HANDLE                            hKmdContext;
    DXGK_SCHEDULING_LOG_CONTEXT_STATE newContextState;
} DXGK_SCHEDULING_LOG_CONTEXT_STATE_CHANGE;

typedef struct _DXGK_SCHEDULING_LOG_HEADER
{
    union
    {
        struct
        {
            UINT32 FirstFreeEntryIndex;
            UINT32 WraparoundCount;
        };
        ULARGE_INTEGER AtomicWraparoundAndEntryIndex;
    };
    UINT64 NumberOfEntries;
    UINT64 Reserved[2];
} DXGK_SCHEDULING_LOG_HEADER;

typedef struct _DXGK_SCHEDULING_LOG_ENTRY
{
    UINT64 GpuTimeStamp;
    UINT   OperationType : 32;
    UINT   ReservedOperationTypeBits : 32;
    union
    {
        DXGK_SCHEDULING_LOG_CONTEXT_STATE_CHANGE ContextStateChange;
        UINT64                                   ReservedOperationData[2];
    };
} DXGK_SCHEDULING_LOG_ENTRY;

typedef struct _DXGK_SCHEDULING_LOG_BUFFER
{
    DXGK_SCHEDULING_LOG_HEADER Header;
    _Field_size_(Header.NumberOfEntries)
    DXGK_SCHEDULING_LOG_ENTRY Entries[1];
} DXGK_SCHEDULING_LOG_BUFFER;

typedef struct _DXGKARG_SETSCHEDULINGLOGBUFFER
{
    UINT NodeOrdinal;
    UINT EngineOrdinal;
    UINT NumberOfEntries;
    _Field_size_bytes_(32 + 32 * NumberOfEntries)
    DXGK_SCHEDULING_LOG_BUFFER *LogBufferCpuVa;
    D3DGPU_VIRTUAL_ADDRESS LogBufferGpuVa;
    _Field_range_(0, NumberOfEntries - 1)
    UINT InterruptEntry;
} DXGKARG_SETSCHEDULINGLOGBUFFER;

typedef _In_ CONST DXGKARG_SETSCHEDULINGLOGBUFFER
    *IN_CONST_PDXGKARG_SETSCHEDULINGLOGBUFFER;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETSCHEDULINGLOGBUFFER)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETSCHEDULINGLOGBUFFER(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETSCHEDULINGLOGBUFFER pSetSchedulingLogBuffer);

typedef enum _DXGK_SCHEDULING_PRIORITY_BAND
{
    DXGK_SCHEDULING_PRIORITY_BAND_IDLE = 0,
    DXGK_SCHEDULING_PRIORITY_BAND_NORMAL = 1,
    DXGK_SCHEDULING_PRIORITY_BAND_FOCUS = 2,
    DXGK_SCHEDULING_PRIORITY_BAND_REALTIME = 3,
    DXGK_SCHEDULING_PRIORITY_BAND_COUNT = 4
} DXGK_SCHEDULING_PRIORITY_BAND;

typedef struct _DXGKARG_SETUPPRIORITYBANDS
{
    UINT64 gracePeriodForBand[DXGK_SCHEDULING_PRIORITY_BAND_COUNT];
    UINT64 processQuantumForBand[DXGK_SCHEDULING_PRIORITY_BAND_COUNT];
    UINT64 processGracePeriodForBand[DXGK_SCHEDULING_PRIORITY_BAND_COUNT];
    UINT   targetNormalBandPercentage;
} DXGKARG_SETUPPRIORITYBANDS;

typedef _In_ CONST DXGKARG_SETUPPRIORITYBANDS
    *IN_CONST_PDXGKARG_SETUPPRIORITYBANDS;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETUPPRIORITYBANDS)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETUPPRIORITYBANDS(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETUPPRIORITYBANDS pSetupPriorityBands);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_NOTIFYFOCUSPRESENT)
    _IRQL_requires_min_(PASSIVE_LEVEL)
    _IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_NOTIFYFOCUSPRESENT(
    IN_CONST_HANDLE hAdapter);

typedef struct _DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES
{
    HANDLE                        hContext;
    DXGK_SCHEDULING_PRIORITY_BAND priorityBand;
    INT                           realtimeBandPriorityLevel;
    INT                           inProcessPriority;
    UINT64                        quantum;
    UINT64                        gracePeriodSamePriority;
    UINT64                        gracePeriodLowerPriority;
} DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES;

typedef _In_ CONST DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES
    *IN_CONST_PDXGKARG_SETCONTEXTSCHEDULINGPROPERTIES;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETCONTEXTSCHEDULINGPROPERTIES)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETCONTEXTSCHEDULINGPROPERTIES(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETCONTEXTSCHEDULINGPROPERTIES
        pSetContextSchedulingProperties);

typedef struct _DXGKARG_SUSPENDCONTEXT
{
    HANDLE hContext;
    UINT64 contextSuspendFence;
} DXGKARG_SUSPENDCONTEXT;

typedef _In_ CONST DXGKARG_SUSPENDCONTEXT
    *IN_CONST_PDXGKARG_SUSPENDCONTEXT;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SUSPENDCONTEXT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SUSPENDCONTEXT(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SUSPENDCONTEXT pSuspendContext);

typedef struct _DXGKARG_RESUMECONTEXT
{
    HANDLE hContext;
} DXGKARG_RESUMECONTEXT;

typedef _In_ CONST DXGKARG_RESUMECONTEXT
    *IN_CONST_PDXGKARG_RESUMECONTEXT;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_RESUMECONTEXT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_RESUMECONTEXT(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RESUMECONTEXT pResumeContext);

typedef struct _DXGK_VIRTUALMACHINEDATAFLAGS
{
    union
    {
        struct
        {
            UINT SecureVirtualMachine : 1;
            UINT LinuxVirtualMachine  : 1;
        };
        UINT Value;
    };
} DXGK_VIRTUALMACHINEDATAFLAGS;

typedef struct _DXGKARG_SETVIRTUALMACHINEDATA
{
    HANDLE                       hKmdVmWorkerProcess;
    GUID                        *pVmGuid;
    DXGK_VIRTUALMACHINEDATAFLAGS Flags;
} DXGKARG_SETVIRTUALMACHINEDATA;

typedef _In_ CONST DXGKARG_SETVIRTUALMACHINEDATA
    *IN_CONST_PDXGKARG_SETVIRTUALMACHINEDATA;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETVIRTUALMACHINEDATA)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETVIRTUALMACHINEDATA(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALMACHINEDATA Args);

typedef struct _DXGKARG_BEGINEXCLUSIVEACCESS
{
    UINT Reserved;
} DXGKARG_BEGINEXCLUSIVEACCESS;

typedef struct _DXGKARG_ENDEXCLUSIVEACCESS
{
    UINT Reserved;
} DXGKARG_ENDEXCLUSIVEACCESS;

typedef _In_ DXGKARG_BEGINEXCLUSIVEACCESS
    *IN_PDXGKARG_BEGINEXCLUSIVEACCESS;
typedef _In_ DXGKARG_ENDEXCLUSIVEACCESS
    *IN_PDXGKARG_ENDEXCLUSIVEACCESS;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_BEGINEXCLUSIVEACCESS)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_BEGINEXCLUSIVEACCESS(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_BEGINEXCLUSIVEACCESS pBeginExclusiveAccess);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_ENDEXCLUSIVEACCESS)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_ENDEXCLUSIVEACCESS(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_ENDEXCLUSIVEACCESS pEndExclusiveAccess);

typedef struct _DXGKARG_RESUMEHWENGINE
{
    UINT NodeOrdinal;
    UINT EngineOrdinal;
} DXGKARG_RESUMEHWENGINE;

typedef _Inout_ DXGKARG_RESUMEHWENGINE
    *INOUT_PDXGKARG_RESUMEHWENGINE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_RESUMEHWENGINE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_RESUMEHWENGINE(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_RESUMEHWENGINE pResumeHwEngine);

typedef DXGKDDI_SETSCHEDULINGLOGBUFFER
    *PDXGKDDI_SETSCHEDULINGLOGBUFFER;
typedef DXGKDDI_SETUPPRIORITYBANDS
    *PDXGKDDI_SETUPPRIORITYBANDS;
typedef DXGKDDI_NOTIFYFOCUSPRESENT
    *PDXGKDDI_NOTIFYFOCUSPRESENT;
typedef DXGKDDI_SETCONTEXTSCHEDULINGPROPERTIES
    *PDXGKDDI_SETCONTEXTSCHEDULINGPROPERTIES;
typedef DXGKDDI_SUSPENDCONTEXT
    *PDXGKDDI_SUSPENDCONTEXT;
typedef DXGKDDI_RESUMECONTEXT
    *PDXGKDDI_RESUMECONTEXT;
typedef DXGKDDI_SETVIRTUALMACHINEDATA
    *PDXGKDDI_SETVIRTUALMACHINEDATA;
typedef DXGKDDI_BEGINEXCLUSIVEACCESS
    *PDXGKDDI_BEGINEXCLUSIVEACCESS;
typedef DXGKDDI_ENDEXCLUSIVEACCESS
    *PDXGKDDI_ENDEXCLUSIVEACCESS;
typedef DXGKDDI_RESUMEHWENGINE
    *PDXGKDDI_RESUMEHWENGINE;

C_ASSERT(sizeof(DXGK_SCHEDULING_LOG_HEADER) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGK_SCHEDULING_LOG_HEADER, NumberOfEntries) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGK_SCHEDULING_LOG_HEADER, Reserved) == 0x10);
C_ASSERT(sizeof(DXGK_SCHEDULING_LOG_ENTRY) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGK_SCHEDULING_LOG_ENTRY, GpuTimeStamp) == 0x0);
C_ASSERT(FIELD_OFFSET(DXGK_SCHEDULING_LOG_ENTRY, ContextStateChange) == 0x10);
C_ASSERT(sizeof(DXGK_SCHEDULING_LOG_BUFFER) == 0x40);
C_ASSERT(FIELD_OFFSET(DXGK_SCHEDULING_LOG_BUFFER, Entries) == 0x20);
C_ASSERT(sizeof(DXGKARG_SETUPPRIORITYBANDS) == 0x68);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETUPPRIORITYBANDS, processQuantumForBand) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETUPPRIORITYBANDS, processGracePeriodForBand) == 0x40);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETUPPRIORITYBANDS, targetNormalBandPercentage) == 0x60);
C_ASSERT(sizeof(DXGKARG_SUSPENDCONTEXT) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUSPENDCONTEXT, contextSuspendFence) == 0x8);
C_ASSERT(sizeof(DXGK_VIRTUALMACHINEDATAFLAGS) == 0x4);
C_ASSERT(sizeof(DXGKARG_BEGINEXCLUSIVEACCESS) == 0x4);
C_ASSERT(sizeof(DXGKARG_ENDEXCLUSIVEACCESS) == 0x4);
C_ASSERT(sizeof(DXGKARG_RESUMEHWENGINE) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_RESUMEHWENGINE, EngineOrdinal) == 0x4);

#ifdef _WIN64
C_ASSERT(sizeof(DXGK_SCHEDULING_LOG_CONTEXT_STATE_CHANGE) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGK_SCHEDULING_LOG_CONTEXT_STATE_CHANGE, newContextState) == 0x8);
C_ASSERT(sizeof(DXGKARG_SETSCHEDULINGLOGBUFFER) == 0x28);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETSCHEDULINGLOGBUFFER, LogBufferCpuVa) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETSCHEDULINGLOGBUFFER, LogBufferGpuVa) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETSCHEDULINGLOGBUFFER, InterruptEntry) == 0x20);
C_ASSERT(sizeof(DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES) == 0x30);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES, priorityBand) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES, quantum) == 0x18);
C_ASSERT(sizeof(DXGKARG_RESUMECONTEXT) == 0x8);
C_ASSERT(sizeof(DXGKARG_SETVIRTUALMACHINEDATA) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIRTUALMACHINEDATA, pVmGuid) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIRTUALMACHINEDATA, Flags) == 0x10);
#else
C_ASSERT(sizeof(DXGK_SCHEDULING_LOG_CONTEXT_STATE_CHANGE) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGK_SCHEDULING_LOG_CONTEXT_STATE_CHANGE, newContextState) == 0x4);
C_ASSERT(sizeof(DXGKARG_SETSCHEDULINGLOGBUFFER) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETSCHEDULINGLOGBUFFER, LogBufferCpuVa) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETSCHEDULINGLOGBUFFER, LogBufferGpuVa) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETSCHEDULINGLOGBUFFER, InterruptEntry) == 0x18);
C_ASSERT(sizeof(DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES) == 0x28);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES, priorityBand) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETCONTEXTSCHEDULINGPROPERTIES, quantum) == 0x10);
C_ASSERT(sizeof(DXGKARG_RESUMECONTEXT) == 0x4);
C_ASSERT(sizeof(DXGKARG_SETVIRTUALMACHINEDATA) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIRTUALMACHINEDATA, pVmGuid) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETVIRTUALMACHINEDATA, Flags) == 0x8);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_4 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)

typedef enum _DXGK_KERNEL_SUBMISSION_TYPE
{
    DXGK_KERNEL_SUBMISSION_BUILD_PAGING_BUFFER = 0,
    DXGK_KERNEL_SUBMISSION_RENDERGDI = 1,
    DXGK_KERNEL_SUBMISSION_PRESENTBLT = 2
} DXGK_KERNEL_SUBMISSION_TYPE;

typedef struct _DXGKARG_SIGNALMONITOREDFENCE
{
    DXGK_KERNEL_SUBMISSION_TYPE KernelSubmissionType;
    VOID                       *pDmaBuffer;
    D3DGPU_VIRTUAL_ADDRESS      DmaBufferGpuVirtualAddress;
    UINT                        DmaSize;
    VOID                       *pDmaBufferPrivateData;
    UINT                        DmaBufferPrivateDataSize;
    UINT                        MultipassOffset;
    D3DGPU_VIRTUAL_ADDRESS      MonitoredFenceGpuVa;
    UINT64                      MonitoredFenceValue;
    VOID                       *MonitoredFenceCpuVa;
    HANDLE                      hHwQueue;
} DXGKARG_SIGNALMONITOREDFENCE;

typedef _Inout_ DXGKARG_SIGNALMONITOREDFENCE
    *INOUT_PDXGKARG_SIGNALMONITOREDFENCE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SIGNALMONITOREDFENCE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SIGNALMONITOREDFENCE(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_SIGNALMONITOREDFENCE pSignalMonitoredFence);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_PRESENTTOHWQUEUE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_PRESENTTOHWQUEUE(
    IN_CONST_HANDLE hHwQueue,
    INOUT_PDXGKARG_PRESENT pPresent);

typedef struct _DXGK_VALIDATESUBMITCOMMANDFLAGS
{
    union
    {
        struct
        {
            UINT HardwareQueueSubmission : 1;
            UINT Reserved                : 31;
        };
        UINT Value;
    };
} DXGK_VALIDATESUBMITCOMMANDFLAGS;

typedef struct _DXGKARG_VALIDATESUBMITCOMMAND
{
    D3DGPU_VIRTUAL_ADDRESS          Commands;
    UINT                            CommandLength;
    DXGK_VALIDATESUBMITCOMMANDFLAGS Flags;
    UINT                            ContextCount;
    HANDLE                          Context[D3DDDI_MAX_BROADCAST_CONTEXT];
    VOID                           *pPrivateDriverData;
    UINT                            PrivateDriverDataSize;
    UINT                            UmdPrivateDataSize;
    UINT64                          HwQueueProgressFenceId;
} DXGKARG_VALIDATESUBMITCOMMAND;

typedef _Inout_ DXGKARG_VALIDATESUBMITCOMMAND
    *INOUT_PDXGKARG_VALIDATESUBMITCOMMAND;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_VALIDATESUBMITCOMMAND)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_VALIDATESUBMITCOMMAND(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_VALIDATESUBMITCOMMAND pArgs);

typedef struct _DXGK_TRACKEDWORKLOAD_STATE_FLAGS
{
    union
    {
        struct
        {
            UINT Saturated    : 1;
            UINT OptimalLevel : 1;
            UINT Reserved     : 30;
        };
        UINT Value;
    };
} DXGK_TRACKEDWORKLOAD_STATE_FLAGS;

typedef struct _DXGKARG_SETTRACKEDWORKLOADPOWERLEVEL
{
    UINT                             PowerLevel;
    UINT                             EffectivePowerLevel;
    DXGK_TRACKEDWORKLOAD_STATE_FLAGS Flags;
} DXGKARG_SETTRACKEDWORKLOADPOWERLEVEL;

typedef _Inout_ DXGKARG_SETTRACKEDWORKLOADPOWERLEVEL
    *INOUT_PDXGKARG_SETTRACKEDWORKLOADPOWERLEVEL;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETTRACKEDWORKLOADPOWERLEVEL)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETTRACKEDWORKLOADPOWERLEVEL(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_SETTRACKEDWORKLOADPOWERLEVEL
        pTrackedWorkloadPowerLevel);

typedef struct _DXGKARGCB_SIGNALEVENT
{
    HANDLE hDxgkProcess;
    HANDLE hEvent;
    union
    {
        struct
        {
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
            UINT CpuEventObject : 1;
            UINT Reserved       : 31;
#else
            UINT Reserved       : 32;
#endif
        };
        UINT Flags;
    };
} DXGKARGCB_SIGNALEVENT;

typedef _In_ CONST DXGKARGCB_SIGNALEVENT
    *IN_CONST_PDXGKARGCB_SIGNALEVENT;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_SIGNALEVENT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_SIGNALEVENT)(
    IN_CONST_PDXGKARGCB_SIGNALEVENT pArgs);

typedef DXGKDDI_SIGNALMONITOREDFENCE
    *PDXGKDDI_SIGNALMONITOREDFENCE;
typedef DXGKDDI_PRESENTTOHWQUEUE
    *PDXGKDDI_PRESENTTOHWQUEUE;
typedef DXGKDDI_VALIDATESUBMITCOMMAND
    *PDXGKDDI_VALIDATESUBMITCOMMAND;
typedef DXGKDDI_SETTRACKEDWORKLOADPOWERLEVEL
    *PDXGKDDI_SETTRACKEDWORKLOADPOWERLEVEL;

C_ASSERT(sizeof(DXGK_KERNEL_SUBMISSION_TYPE) == 0x4);
C_ASSERT(sizeof(DXGK_VALIDATESUBMITCOMMANDFLAGS) == 0x4);
C_ASSERT(sizeof(DXGK_TRACKEDWORKLOAD_STATE_FLAGS) == 0x4);
C_ASSERT(sizeof(DXGKARG_SETTRACKEDWORKLOADPOWERLEVEL) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETTRACKEDWORKLOADPOWERLEVEL, EffectivePowerLevel) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETTRACKEDWORKLOADPOWERLEVEL, Flags) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SIGNALEVENT, Flags) == (2 * sizeof(HANDLE)));

#ifdef _WIN64
C_ASSERT(sizeof(DXGKARGCB_SIGNALEVENT) == 0x18);
C_ASSERT(sizeof(DXGKARG_SIGNALMONITOREDFENCE) == 0x50);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, pDmaBuffer) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, DmaBufferGpuVirtualAddress) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, pDmaBufferPrivateData) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, MonitoredFenceGpuVa) == 0x30);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, MonitoredFenceValue) == 0x38);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, MonitoredFenceCpuVa) == 0x40);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, hHwQueue) == 0x48);
C_ASSERT(sizeof(DXGKARG_VALIDATESUBMITCOMMAND) == 0x230);
C_ASSERT(FIELD_OFFSET(DXGKARG_VALIDATESUBMITCOMMAND, Context) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARG_VALIDATESUBMITCOMMAND, pPrivateDriverData) == 0x218);
C_ASSERT(FIELD_OFFSET(DXGKARG_VALIDATESUBMITCOMMAND, HwQueueProgressFenceId) == 0x228);
#else
C_ASSERT(sizeof(DXGKARGCB_SIGNALEVENT) == 0xC);
C_ASSERT(sizeof(DXGKARG_SIGNALMONITOREDFENCE) == 0x38);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, pDmaBuffer) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, DmaBufferGpuVirtualAddress) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, pDmaBufferPrivateData) == 0x14);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, MonitoredFenceGpuVa) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, MonitoredFenceValue) == 0x28);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, MonitoredFenceCpuVa) == 0x30);
C_ASSERT(FIELD_OFFSET(DXGKARG_SIGNALMONITOREDFENCE, hHwQueue) == 0x34);
C_ASSERT(sizeof(DXGKARG_VALIDATESUBMITCOMMAND) == 0x128);
C_ASSERT(FIELD_OFFSET(DXGKARG_VALIDATESUBMITCOMMAND, Context) == 0x14);
C_ASSERT(FIELD_OFFSET(DXGKARG_VALIDATESUBMITCOMMAND, pPrivateDriverData) == 0x114);
C_ASSERT(FIELD_OFFSET(DXGKARG_VALIDATESUBMITCOMMAND, HwQueueProgressFenceId) == 0x120);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)

typedef struct _DXGK_KSR_MEMORY_RANGE
{
    ULONGLONG MemoryRangeDesc;
} DXGK_KSR_MEMORY_RANGE, *PDXGK_KSR_MEMORY_RANGE;

typedef struct _DXGKARGCB_SAVEMEMORYFORHOTUPDATE
{
    UINT                    NumDataMemoryRanges;
    _Field_size_(NumDataMemoryRanges)
    DXGK_KSR_MEMORY_RANGE  *pDataMemoryRanges;
    PMDL                    pDataMdl;
    UINT                    DataSize;
    _Field_size_(DataSize)
    PVOID                   pData;
    UINT                    MetaDataSize;
    _Field_size_(MetaDataSize)
    BYTE                   *pMetaData;
} DXGKARGCB_SAVEMEMORYFORHOTUPDATE;

typedef _In_ CONST DXGKARGCB_SAVEMEMORYFORHOTUPDATE
    *IN_CONST_PDXGKARGCB_SAVEMEMORYFORHOTUPDATE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_SAVEMEMORYFORHOTUPDATE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_SAVEMEMORYFORHOTUPDATE)(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARGCB_SAVEMEMORYFORHOTUPDATE pArgs);

typedef struct _DXGK_SAVEMEMORYFORHOTUPDATEFLAGS
{
    union
    {
        struct
        {
            UINT CancelHotUpdate : 1;
            UINT Reserved        : 31;
        };
        UINT Value;
    };
} DXGK_SAVEMEMORYFORHOTUPDATEFLAGS;

typedef struct _DXGKARG_SAVEMEMORYFORHOTUPDATE
{
    DXGK_SAVEMEMORYFORHOTUPDATEFLAGS Flags;
} DXGKARG_SAVEMEMORYFORHOTUPDATE;

typedef _In_ CONST DXGKARG_SAVEMEMORYFORHOTUPDATE
    *IN_CONST_PDXGKARG_SAVEMEMORYFORHOTUPDATE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SAVEMEMORYFORHOTUPDATE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SAVEMEMORYFORHOTUPDATE(
    IN_CONST_HANDLE hContext,
    IN_CONST_PDXGKARG_SAVEMEMORYFORHOTUPDATE pArgs);

typedef struct _DXGK_RESTOREMEMORYFORHOTUPDATEFLAGS
{
    union
    {
        struct
        {
            UINT RestoreComplete : 1;
            UINT Reserved        : 31;
        };
        UINT Value;
    };
} DXGK_RESTOREMEMORYFORHOTUPDATEFLAGS;

typedef struct _DXGKARG_RESTOREMEMORYFORHOTUPDATE
{
    DXGK_RESTOREMEMORYFORHOTUPDATEFLAGS Flags;
    PMDL                                pDataMdl;
    UINT                                MetaDataSize;
    _Field_size_(MetaDataSize)
    PVOID                               pMetaData;
} DXGKARG_RESTOREMEMORYFORHOTUPDATE;

typedef _In_ CONST DXGKARG_RESTOREMEMORYFORHOTUPDATE
    *IN_CONST_PDXGKARG_RESTOREMEMORYFORHOTUPDATE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_RESTOREMEMORYFORHOTUPDATE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_RESTOREMEMORYFORHOTUPDATE(
    IN_CONST_HANDLE hContext,
    IN_CONST_PDXGKARG_RESTOREMEMORYFORHOTUPDATE pArgs);

typedef DXGKDDI_SAVEMEMORYFORHOTUPDATE
    *PDXGKDDI_SAVEMEMORYFORHOTUPDATE;
typedef DXGKDDI_RESTOREMEMORYFORHOTUPDATE
    *PDXGKDDI_RESTOREMEMORYFORHOTUPDATE;

C_ASSERT(sizeof(DXGK_KSR_MEMORY_RANGE) == 0x8);
C_ASSERT(sizeof(DXGK_SAVEMEMORYFORHOTUPDATEFLAGS) == 0x4);
C_ASSERT(sizeof(DXGKARG_SAVEMEMORYFORHOTUPDATE) == 0x4);
C_ASSERT(sizeof(DXGK_RESTOREMEMORYFORHOTUPDATEFLAGS) == 0x4);

#ifdef _WIN64
C_ASSERT(sizeof(DXGKARGCB_SAVEMEMORYFORHOTUPDATE) == 0x38);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, pDataMemoryRanges) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, pDataMdl) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, DataSize) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, pData) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, MetaDataSize) == 0x28);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, pMetaData) == 0x30);
C_ASSERT(sizeof(DXGKARG_RESTOREMEMORYFORHOTUPDATE) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_RESTOREMEMORYFORHOTUPDATE, pDataMdl) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_RESTOREMEMORYFORHOTUPDATE, MetaDataSize) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_RESTOREMEMORYFORHOTUPDATE, pMetaData) == 0x18);
#else
C_ASSERT(sizeof(DXGKARGCB_SAVEMEMORYFORHOTUPDATE) == 0x1C);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, pDataMemoryRanges) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, pDataMdl) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, DataSize) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, pData) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, MetaDataSize) == 0x14);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_SAVEMEMORYFORHOTUPDATE, pMetaData) == 0x18);
C_ASSERT(sizeof(DXGKARG_RESTOREMEMORYFORHOTUPDATE) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_RESTOREMEMORYFORHOTUPDATE, pDataMdl) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_RESTOREMEMORYFORHOTUPDATE, MetaDataSize) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_RESTOREMEMORYFORHOTUPDATE, pMetaData) == 0xC);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_6 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)

typedef struct _DXGKARG_CONTROLINTERRUPT3
{
    DXGK_INTERRUPT_TYPE InterruptType;
    union
    {
        DXGK_INTERRUPT_STATE  InterruptState;
        DXGK_CRTC_VSYNC_STATE CrtcVsyncState;
    };
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} DXGKARG_CONTROLINTERRUPT3;

typedef _In_ CONST DXGKARG_CONTROLINTERRUPT3
    *IN_CONST_PDXGKARG_CONTROLINTERRUPT3;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CONTROLINTERRUPT3)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_CONTROLINTERRUPT3(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_CONTROLINTERRUPT3 InterruptControl);

typedef DXGKDDI_CONTROLINTERRUPT3
    *PDXGKDDI_CONTROLINTERRUPT3;

C_ASSERT(sizeof(DXGK_INTERRUPT_STATE) == 0x4);
C_ASSERT(sizeof(DXGK_CRTC_VSYNC_STATE) == 0x4);
C_ASSERT(sizeof(DXGKARG_CONTROLINTERRUPT3) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_CONTROLINTERRUPT3, InterruptType) == 0x0);
C_ASSERT(FIELD_OFFSET(DXGKARG_CONTROLINTERRUPT3, InterruptState) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_CONTROLINTERRUPT3, CrtcVsyncState) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_CONTROLINTERRUPT3, VidPnSourceId) == 0x8);

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_7 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)

typedef struct _DXGKARGCB_NOTIFYCURSORSUPPORTCHANGE
{
    HANDLE                         DeviceHandle;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} DXGKARGCB_NOTIFYCURSORSUPPORTCHANGE;

typedef _In_ CONST DXGKARGCB_NOTIFYCURSORSUPPORTCHANGE
    *IN_CONST_PDXGKARGCB_NOTIFYCURSORSUPPORTCHANGE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_NOTIFYCURSORSUPPORTCHANGE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_NOTIFYCURSORSUPPORTCHANGE)(
    IN_CONST_PDXGKARGCB_NOTIFYCURSORSUPPORTCHANGE pArgs);

#ifdef _WIN64
C_ASSERT(sizeof(DXGKARGCB_NOTIFYCURSORSUPPORTCHANGE) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFYCURSORSUPPORTCHANGE, VidPnSourceId) == 0x8);
#if (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_9)
C_ASSERT(sizeof(DXGK_DRIVERCAPS) == 0x248);
C_ASSERT(FIELD_OFFSET(DXGK_DRIVERCAPS, MiscCaps) == 0x240);
#endif
#else
C_ASSERT(sizeof(DXGKARGCB_NOTIFYCURSORSUPPORTCHANGE) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_NOTIFYCURSORSUPPORTCHANGE, VidPnSourceId) == 0x4);
#if (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_9)
C_ASSERT(sizeof(DXGK_DRIVERCAPS) == 0x240);
C_ASSERT(FIELD_OFFSET(DXGK_DRIVERCAPS, MiscCaps) == 0x238);
#endif
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_8 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)

typedef struct _DXGK_FLIPQUEUE_LOG_ENTRY
{
    ULONGLONG PresentId;
    ULONGLONG PresentTimestamp;
} DXGK_FLIPQUEUE_LOG_ENTRY;

typedef struct _DXGKARG_SETFLIPQUEUELOGBUFFER
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT                           LayerIndex;
    UINT                           NumberOfEntries;
    _Field_size_(NumberOfEntries)
    _Maybenull_
    DXGK_FLIPQUEUE_LOG_ENTRY      *LogBufferAddress;
} DXGKARG_SETFLIPQUEUELOGBUFFER;

typedef _In_ CONST DXGKARG_SETFLIPQUEUELOGBUFFER
    *IN_CONST_PDXGKARG_SETFLIPQUEUELOGBUFFER;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETFLIPQUEUELOGBUFFER)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETFLIPQUEUELOGBUFFER(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETFLIPQUEUELOGBUFFER pSetFlipQueueLogBuffer);

typedef struct _DXGKARG_UPDATEFLIPQUEUELOG
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT                           LayerIndex;
    ULONG                          FirstFreeFlipQueueLogEntryIndex;
} DXGKARG_UPDATEFLIPQUEUELOG;

typedef _Inout_ DXGKARG_UPDATEFLIPQUEUELOG
    *INOUT_PDXGKARG_UPDATEFLIPQUEUELOG;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_UPDATEFLIPQUEUELOG)
    _IRQL_requires_max_(PROFILE_LEVEL - 1)
NTSTATUS
APIENTRY
DXGKDDI_UPDATEFLIPQUEUELOG(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_UPDATEFLIPQUEUELOG pUpdateFlipQueueLog);

typedef struct _DXGKARG_CANCELQUEUEDFLIPS
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT                           LayerIndex;
    ULONGLONG                      PresentIdCancelRequested;
    ULONGLONG                      PresentIdCancelled;
} DXGKARG_CANCELQUEUEDFLIPS;

typedef _Inout_ DXGKARG_CANCELQUEUEDFLIPS
    *INOUT_PDXGKARG_CANCELQUEUEDFLIPS;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CANCELQUEUEDFLIPS)
    _IRQL_requires_max_(PROFILE_LEVEL - 1)
NTSTATUS
APIENTRY
DXGKDDI_CANCELQUEUEDFLIPS(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CANCELQUEUEDFLIPS pCancelQueuedFlips);

typedef struct _DXGK_CANCELFLIPS_PLANE
{
    ULONGLONG PresentIdCancelRequested;
    ULONGLONG PresentIdCancelled;
    UINT      LayerIndex;
} DXGK_CANCELFLIPS_PLANE;

typedef struct _DXGKARG_CANCELFLIPS
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT                           PlaneCount;
    _Field_size_(PlaneCount)
    DXGK_CANCELFLIPS_PLANE       **ppPlanes;
} DXGKARG_CANCELFLIPS;

typedef _Inout_ DXGKARG_CANCELFLIPS
    *INOUT_PDXGKARG_CANCELFLIPS;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CANCELFLIPS)
    _IRQL_requires_max_(PROFILE_LEVEL - 1)
NTSTATUS
APIENTRY
DXGKDDI_CANCELFLIPS(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CANCELFLIPS pCancelFlips);

typedef struct _DXGKARG_SETINTERRUPTTARGETPRESENTID
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT                           LayerIndex;
    ULONGLONG                      InterruptTargetPresentId;
} DXGKARG_SETINTERRUPTTARGETPRESENTID;

typedef _In_ CONST DXGKARG_SETINTERRUPTTARGETPRESENTID
    *IN_CONST_PDXGKARG_SETINTERRUPTTARGETPRESENTID;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETINTERRUPTTARGETPRESENTID)
    _IRQL_requires_max_(PROFILE_LEVEL - 1)
NTSTATUS
APIENTRY
DXGKDDI_SETINTERRUPTTARGETPRESENTID(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETINTERRUPTTARGETPRESENTID
        pSetInterruptTargetPresentId);

typedef DXGKDDI_SETFLIPQUEUELOGBUFFER
    *PDXGKDDI_SETFLIPQUEUELOGBUFFER;
typedef DXGKDDI_UPDATEFLIPQUEUELOG
    *PDXGKDDI_UPDATEFLIPQUEUELOG;
typedef DXGKDDI_CANCELQUEUEDFLIPS
    *PDXGKDDI_CANCELQUEUEDFLIPS;
typedef DXGKDDI_SETINTERRUPTTARGETPRESENTID
    *PDXGKDDI_SETINTERRUPTTARGETPRESENTID;

typedef enum _DXGK_PHYSICAL_MEMORY_TYPE
{
    DXGK_PHYSICAL_MEMORY_TYPE_MDL,
    DXGK_PHYSICAL_MEMORY_TYPE_CONTIGUOUS_MEMORY,
    DXGK_PHYSICAL_MEMORY_TYPE_SECTION,
    DXGK_PHYSICAL_MEMORY_TYPE_IO_SPACE
} DXGK_PHYSICAL_MEMORY_TYPE;

typedef enum _DXGK_ACCESS_MODE
{
    DXGK_ACCESS_MODE_KERNEL_MODE,
    DXGK_ACCESS_MODE_USER_MODE
} DXGK_ACCESS_MODE;

typedef struct _OBJECT_ATTRIBUTES *POBJECT_ATTRIBUTES;

typedef struct _DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT
{
    _In_opt_ HANDLE                    hAdapter;
    _In_     SIZE_T                    Size;
    _In_opt_ ULONG_PTR                 Context;
    _In_     DXGK_PHYSICAL_MEMORY_TYPE Type;
    _In_     DXGK_MEMORY_CACHING_TYPE  CacheType;
    union
    {
        struct
        {
            _In_ PHYSICAL_ADDRESS LowAddress;
            _In_ PHYSICAL_ADDRESS HighAddress;
            _In_ PHYSICAL_ADDRESS SkipBytes;
            _In_ UINT             Flags;
        } Mdl;
        struct
        {
            _In_ PHYSICAL_ADDRESS LowestAcceptableAddress;
            _In_ PHYSICAL_ADDRESS HighestAcceptableAddress;
            _In_ PHYSICAL_ADDRESS BoundaryAddressMultiple;
        } ContiguousMemory;
        struct
        {
            _In_ ACCESS_MASK        DesiredAccess;
            _In_ POBJECT_ATTRIBUTES ObjectAttributes;
            _In_ ULONG              PageProtection;
            _In_ ULONG              AllocationAttributes;
        } Section;
        struct
        {
            _In_ PHYSICAL_ADDRESS BaseAddress;
        } IOSpace;
    };
    _Out_ HANDLE hPhysicalMemoryObject;
    _Out_ HANDLE hAdapterMemoryObject;
} DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT;

typedef struct _DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT
{
    _In_     HANDLE hPhysicalMemoryObject;
    _In_opt_ HANDLE hAdapterMemoryObject;
} DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT;

typedef _Inout_ DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT
    *IN_OUT_PDXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT;
typedef _In_ CONST DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT
    *IN_CONST_PDXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_CREATEPHYSICALMEMORYOBJECT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_CREATEPHYSICALMEMORYOBJECT)(
    IN_OUT_PDXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT pArgs);

typedef
    _Function_class_DXGK_(DXGKCB_DESTROYPHYSICALMEMORYOBJECT)
    _IRQL_requires_(PASSIVE_LEVEL)
VOID
(APIENTRY CALLBACK *DXGKCB_DESTROYPHYSICALMEMORYOBJECT)(
    IN_CONST_PDXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT pArgs);

typedef struct _DXGKARGCB_MAP_PHYSICAL_MEMORY
{
    _In_  HANDLE           hPhysicalMemoryObject;
    _In_  DXGK_ACCESS_MODE AccessMode;
    _In_  SIZE_T           Offset;
    _In_  SIZE_T           Size;
    _Out_ void            *pMappedAddress;
} DXGKARGCB_MAP_PHYSICAL_MEMORY;

typedef struct _DXGKARGCB_UNMAP_PHYSICAL_MEMORY
{
    _In_ HANDLE hPhysicalMemoryObject;
    _In_ void  *pBaseAddress;
    _In_ SIZE_T Size;
} DXGKARGCB_UNMAP_PHYSICAL_MEMORY;

typedef _Inout_ DXGKARGCB_MAP_PHYSICAL_MEMORY
    *IN_OUT_PDXGKARGCB_MAP_PHYSICAL_MEMORY;
typedef _In_ CONST DXGKARGCB_UNMAP_PHYSICAL_MEMORY
    *IN_CONST_PDXGKARGCB_UNMAP_PHYSICAL_MEMORY;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_MAPPHYSICALMEMORY)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_MAPPHYSICALMEMORY)(
    IN_OUT_PDXGKARGCB_MAP_PHYSICAL_MEMORY pArgs);

typedef
    _Function_class_DXGK_(DXGKCB_UNMAPPHYSICALMEMORY)
    _IRQL_requires_(PASSIVE_LEVEL)
VOID
(APIENTRY CALLBACK *DXGKCB_UNMAPPHYSICALMEMORY)(
    IN_CONST_PDXGKARGCB_UNMAP_PHYSICAL_MEMORY pArgs);

typedef struct _DXGKARGCB_ALLOCATE_ADL
{
    _In_ HANDLE hAdapterMemoryObject;
    _In_ SIZE_T Offset;
    _In_ SIZE_T Size;
    union
    {
        struct
        {
            UINT32 RequireContiguous : 1;
            UINT32 PreferContiguous  : 1;
            UINT32 Reserved          : 30;
        };
        UINT32 Value;
    } Flags;
    _Out_ DXGK_ADL *pAdl;
} DXGKARGCB_ALLOCATE_ADL;

typedef struct _DXGKARGCB_FREE_ADL
{
    _In_ HANDLE    hAdapterMemoryObject;
    _In_ DXGK_ADL *pAdl;
} DXGKARGCB_FREE_ADL;

typedef _Inout_ DXGKARGCB_ALLOCATE_ADL
    *IN_OUT_PDXGKARGCB_ALLOCATE_ADL;
typedef _In_ CONST DXGKARGCB_FREE_ADL
    *IN_CONST_PDXGKARGCB_FREE_ADL;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_ALLOCATEADL)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_ALLOCATEADL)(
    IN_OUT_PDXGKARGCB_ALLOCATE_ADL pArgs);

typedef
    _Function_class_DXGK_(DXGKCB_FREEADL)
    _IRQL_requires_(PASSIVE_LEVEL)
VOID
(APIENTRY CALLBACK *DXGKCB_FREEADL)(
    IN_CONST_PDXGKARGCB_FREE_ADL pArgs);

typedef struct _DXGKARGCB_OPEN_PHYSICAL_MEMORY_OBJECT
{
    _In_  HANDLE hPhysicalMemoryObject;
    _In_  HANDLE hAdapter;
    _Out_ HANDLE hAdapterMemoryObject;
} DXGKARGCB_OPEN_PHYSICAL_MEMORY_OBJECT;

typedef struct _DXGKARGCB_CLOSE_PHYSICAL_MEMORY_OBJECT
{
    _In_ HANDLE hAdapterMemoryObject;
} DXGKARGCB_CLOSE_PHYSICAL_MEMORY_OBJECT;

typedef _Inout_ DXGKARGCB_OPEN_PHYSICAL_MEMORY_OBJECT
    *IN_OUT_PDXGKARGCB_OPEN_PHYSICAL_MEMORY_OBJECT;
typedef _In_ CONST DXGKARGCB_CLOSE_PHYSICAL_MEMORY_OBJECT
    *IN_CONST_PDXGKARGCB_CLOSE_PHYSICAL_MEMORY_OBJECT;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_OPENPHYSICALMEMORYOBJECT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_OPENPHYSICALMEMORYOBJECT)(
    IN_OUT_PDXGKARGCB_OPEN_PHYSICAL_MEMORY_OBJECT pArgs);

typedef
    _Function_class_DXGK_(DXGKCB_CLOSEPHYSICALMEMORYOBJECT)
    _IRQL_requires_(PASSIVE_LEVEL)
VOID
(APIENTRY CALLBACK *DXGKCB_CLOSEPHYSICALMEMORYOBJECT)(
    IN_CONST_PDXGKARGCB_CLOSE_PHYSICAL_MEMORY_OBJECT pArgs);

typedef struct _DXGKARGCB_QUERYFEATURESUPPORT
{
    HANDLE          DeviceHandle;
    DXGK_FEATURE_ID FeatureId;
    _Field_range_(DXGK_FEATURE_SUPPORT_ALWAYS_OFF,
                  DXGK_FEATURE_SUPPORT_ALWAYS_ON)
    UINT            DriverSupportState;
    BOOLEAN         Enabled;
} DXGKARGCB_QUERYFEATURESUPPORT;

typedef _Inout_ DXGKARGCB_QUERYFEATURESUPPORT
    *INOUT_PDXGKARGCB_QUERYFEATURESUPPORT;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_QUERYFEATURESUPPORT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_QUERYFEATURESUPPORT)(
    INOUT_PDXGKARGCB_QUERYFEATURESUPPORT pArgs);

typedef struct _DXGKARGCB_PINFRAMEBUFFERFORSAVE2
{
    _In_ UINT   PhysicalAdapterIndex;
    _In_ SIZE_T CommitSize;
    union
    {
        struct
        {
            UINT PreferContiguous : 1;
            UINT Reserved         : 31;
        };
        UINT Value;
    } Flags;
    _Out_ DXGK_ADL *pAdl;
} DXGKARGCB_PINFRAMEBUFFERFORSAVE2;

typedef _Inout_ DXGKARGCB_PINFRAMEBUFFERFORSAVE2
    *INOUT_PDXGKARGCB_PINFRAMEBUFFERFORSAVE2;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_PINFRAMEBUFFERFORSAVE2)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_PINFRAMEBUFFERFORSAVE2)(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARGCB_PINFRAMEBUFFERFORSAVE2 pPinFrameBufferForSave2);

C_ASSERT(DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY3 == 18);
C_ASSERT(sizeof(DXGK_FLIPQUEUE_LOG_ENTRY) == 0x10);
C_ASSERT(sizeof(DXGKARG_UPDATEFLIPQUEUELOG) == 0xC);
C_ASSERT(sizeof(DXGKARG_CANCELQUEUEDFLIPS) == 0x18);
C_ASSERT(sizeof(DXGK_CANCELFLIPS_PLANE) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGK_CANCELFLIPS_PLANE, LayerIndex) == 0x10);
C_ASSERT(sizeof(DXGKARG_SETINTERRUPTTARGETPRESENTID) == 0x10);
C_ASSERT(sizeof(DXGK_PHYSICAL_MEMORY_CAPS) == 0x8);
C_ASSERT(sizeof(DXGK_IOMMU_CAPS) == 0x4);
C_ASSERT(sizeof(DXGK_MEMORY_CACHING_TYPE) == 0x4);
C_ASSERT(sizeof(DXGK_PHYSICAL_MEMORY_TYPE) == 0x4);
C_ASSERT(sizeof(DXGK_ACCESS_MODE) == 0x4);
C_ASSERT(sizeof(DXGK_ADL_FLAGS) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGK_ADL, Flags) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGK_ADL, BasePageNumber) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGK_ADL, Pages) == 0x8);

#ifdef _WIN64
C_ASSERT(sizeof(DXGKARG_SETFLIPQUEUELOGBUFFER) == 0x18);
C_ASSERT(sizeof(DXGKARG_CANCELFLIPS) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETFLIPQUEUELOGBUFFER,
                      LogBufferAddress) == 0x10);
C_ASSERT(sizeof(DXGK_ADL) == 0x10);
C_ASSERT(sizeof(DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT) == 0x50);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT,
                      hPhysicalMemoryObject) == 0x40);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT,
                      hAdapterMemoryObject) == 0x48);
C_ASSERT(sizeof(DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT) == 0x10);
C_ASSERT(sizeof(DXGKARGCB_MAP_PHYSICAL_MEMORY) == 0x28);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_MAP_PHYSICAL_MEMORY,
                      pMappedAddress) == 0x20);
C_ASSERT(sizeof(DXGKARGCB_UNMAP_PHYSICAL_MEMORY) == 0x18);
C_ASSERT(sizeof(DXGKARGCB_ALLOCATE_ADL) == 0x28);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_ALLOCATE_ADL, pAdl) == 0x20);
C_ASSERT(sizeof(DXGKARGCB_FREE_ADL) == 0x10);
C_ASSERT(sizeof(DXGKARGCB_OPEN_PHYSICAL_MEMORY_OBJECT) == 0x18);
C_ASSERT(sizeof(DXGKARGCB_CLOSE_PHYSICAL_MEMORY_OBJECT) == 0x8);
C_ASSERT(sizeof(DXGKARGCB_QUERYFEATURESUPPORT) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_QUERYFEATURESUPPORT, Enabled) == 0x10);
C_ASSERT(sizeof(DXGKARGCB_PINFRAMEBUFFERFORSAVE2) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_PINFRAMEBUFFERFORSAVE2, pAdl) == 0x18);
#else
C_ASSERT(sizeof(DXGKARG_SETFLIPQUEUELOGBUFFER) == 0x10);
C_ASSERT(sizeof(DXGKARG_CANCELFLIPS) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_SETFLIPQUEUELOGBUFFER,
                      LogBufferAddress) == 0xC);
C_ASSERT(sizeof(DXGK_ADL) == 0xC);
C_ASSERT(sizeof(DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT) == 0x40);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT,
                      hPhysicalMemoryObject) == 0x38);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT,
                      hAdapterMemoryObject) == 0x3C);
C_ASSERT(sizeof(DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT) == 0x8);
C_ASSERT(sizeof(DXGKARGCB_MAP_PHYSICAL_MEMORY) == 0x14);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_MAP_PHYSICAL_MEMORY,
                      pMappedAddress) == 0x10);
C_ASSERT(sizeof(DXGKARGCB_UNMAP_PHYSICAL_MEMORY) == 0xC);
C_ASSERT(sizeof(DXGKARGCB_ALLOCATE_ADL) == 0x14);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_ALLOCATE_ADL, pAdl) == 0x10);
C_ASSERT(sizeof(DXGKARGCB_FREE_ADL) == 0x8);
C_ASSERT(sizeof(DXGKARGCB_OPEN_PHYSICAL_MEMORY_OBJECT) == 0xC);
C_ASSERT(sizeof(DXGKARGCB_CLOSE_PHYSICAL_MEMORY_OBJECT) == 0x4);
C_ASSERT(sizeof(DXGKARGCB_QUERYFEATURESUPPORT) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_QUERYFEATURESUPPORT, Enabled) == 0xC);
C_ASSERT(sizeof(DXGKARGCB_PINFRAMEBUFFERFORSAVE2) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_PINFRAMEBUFFERFORSAVE2, pAdl) == 0xC);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)

typedef struct _DXGKARG_SETALLOCATIONBACKINGSTORE
{
    HANDLE hDriverAllocation;
    VOID  *pBackingStore;
} DXGKARG_SETALLOCATIONBACKINGSTORE;

typedef _In_ CONST DXGKARG_SETALLOCATIONBACKINGSTORE
    *IN_CONST_PDXGKARG_SETALLOCATIONBACKINGSTORE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETALLOCATIONBACKINGSTORE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETALLOCATIONBACKINGSTORE(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETALLOCATIONBACKINGSTORE pArgs);

typedef struct _DXGK_CREATECPUEVENTFLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGK_CREATECPUEVENTFLAGS;

typedef struct _DXGKARG_CREATECPUEVENT
{
    HANDLE                    hKmdDevice;
    HANDLE                    hDxgCpuEvent;
    DXGK_CREATECPUEVENTFLAGS Flags;
    HANDLE                    hKmdCpuEvent;
} DXGKARG_CREATECPUEVENT;

typedef _Inout_ DXGKARG_CREATECPUEVENT
    *INOUT_PDXGKARG_CREATECPUEVENT;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CREATECPUEVENT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_CREATECPUEVENT(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATECPUEVENT pArgs);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DESTROYCPUEVENT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_DESTROYCPUEVENT(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hKmdCpuEvent);

typedef DXGKDDI_SETALLOCATIONBACKINGSTORE
    *PDXGKDDI_SETALLOCATIONBACKINGSTORE;
typedef DXGKDDI_CREATECPUEVENT
    *PDXGKDDI_CREATECPUEVENT;
typedef DXGKDDI_DESTROYCPUEVENT
    *PDXGKDDI_DESTROYCPUEVENT;
typedef DXGKDDI_CANCELFLIPS
    *PDXGKDDI_CANCELFLIPS;

DEFINE_GUID(GUID_DEVINTERFACE_WDDM3_ON_VB,
            0xe922004d, 0xeb9c, 0x4de1,
            0x92, 0x24, 0xa9, 0xce, 0xaa, 0x95, 0x9b, 0xce);

#if !defined(_WDMDDK_) && !defined(_WDM_INCLUDED_) && \
    !defined(_NTDDK_) && !defined(_NTIFS_)
typedef VOID (*PINTERFACE_REFERENCE)(PVOID Context);
typedef VOID (*PINTERFACE_DEREFERENCE)(PVOID Context);
#endif

typedef struct _DXGK_WDDM3_ON_VB_INTERFACE
{
    IN  USHORT                              Size;
    IN  USHORT                              Version;
    OUT PVOID                               Context;
    OUT PINTERFACE_REFERENCE                InterfaceReference;
    OUT PINTERFACE_DEREFERENCE              InterfaceDereference;
    OUT PDXGKDDI_SETALLOCATIONBACKINGSTORE DxgkDdiSetAllocationBackingStore;
    OUT PDXGKDDI_CREATECPUEVENT            DxgkDdiCreateCpuEvent;
    OUT PDXGKDDI_DESTROYCPUEVENT           DxgkDdiDestroyCpuEvent;
} DXGK_WDDM3_ON_VB_INTERFACE, *PDXGK_WDDM3_ON_VB_INTERFACE;

#define DXGK_WDDM3_ON_VB_INTERFACE_VERSION_1 1

C_ASSERT(sizeof(DXGK_CREATECPUEVENTFLAGS) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECPUEVENT, Flags) == (2 * sizeof(HANDLE)));
#ifdef _WIN64
C_ASSERT(sizeof(DXGKARG_SETALLOCATIONBACKINGSTORE) == 0x10);
C_ASSERT(sizeof(DXGKARG_CREATECPUEVENT) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECPUEVENT, hKmdCpuEvent) == 0x18);
C_ASSERT(sizeof(DXGK_WDDM3_ON_VB_INTERFACE) == 0x38);
C_ASSERT(FIELD_OFFSET(DXGK_WDDM3_ON_VB_INTERFACE,
                      DxgkDdiSetAllocationBackingStore) == 0x20);
#else
C_ASSERT(sizeof(DXGKARG_SETALLOCATIONBACKINGSTORE) == 0x8);
C_ASSERT(sizeof(DXGKARG_CREATECPUEVENT) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATECPUEVENT, hKmdCpuEvent) == 0xC);
C_ASSERT(sizeof(DXGK_WDDM3_ON_VB_INTERFACE) == 0x1C);
C_ASSERT(FIELD_OFFSET(DXGK_WDDM3_ON_VB_INTERFACE,
                      DxgkDdiSetAllocationBackingStore) == 0x10);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_0 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)

typedef struct _DXGK_NATIVE_FENCE_CAPS
{
    UINT MonitoredValueStride;
    BOOLEAN MapToGpuSystemProcess;
    D3DGPU_VIRTUAL_ADDRESS MinimumAddress;
    D3DGPU_VIRTUAL_ADDRESS MaximumAddress;
    BYTE Reserved[28];
} DXGK_NATIVE_FENCE_CAPS;

typedef struct _DXGKARG_CREATENATIVEFENCE_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGKARG_CREATENATIVEFENCE_FLAGS;

typedef struct _DXGKARG_CREATENATIVEFENCE
{
    HANDLE hGlobalNativeFence;
    D3DDDI_NATIVEFENCE_TYPE Type;
    D3DGPU_VIRTUAL_ADDRESS CurrentValueSystemProcessGpuVa;
    D3DGPU_VIRTUAL_ADDRESS MonitoredValueSystemProcessGpuVa;
    BYTE pPrivateDriverData[D3DDDI_NATIVE_FENCE_PDD_SIZE];
    DXGKARG_CREATENATIVEFENCE_FLAGS Flags;
    BYTE Reserved[32];
} DXGKARG_CREATENATIVEFENCE;

typedef _Inout_ DXGKARG_CREATENATIVEFENCE
    *INOUT_PDXGKARG_CREATENATIVEFENCE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CREATENATIVEFENCE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_CREATENATIVEFENCE(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATENATIVEFENCE pCreateNativeFence);

typedef struct _DXGK_OPENNATIVEFENCE_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGK_OPENNATIVEFENCE_FLAGS;

typedef struct _DXGKARG_OPENNATIVEFENCE
{
    HANDLE hGlobalNativeFence;
    HANDLE hLocalNativeFence;
    HANDLE hDevice;
    D3DGPU_VIRTUAL_ADDRESS CurrentValueGpuVa;
    D3DGPU_VIRTUAL_ADDRESS MonitoredValueGpuVa;
    DXGK_OPENNATIVEFENCE_FLAGS Flags;
    BYTE Reserved[32];
} DXGKARG_OPENNATIVEFENCE;

typedef _Inout_ DXGKARG_OPENNATIVEFENCE
    *INOUT_PDXGKARG_OPENNATIVEFENCE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_OPENNATIVEFENCE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_OPENNATIVEFENCE(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_OPENNATIVEFENCE pOpenNativeFence);

typedef struct _DXGK_CLOSENATIVEFENCE_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGK_CLOSENATIVEFENCE_FLAGS;

typedef struct _DXGKARG_CLOSENATIVEFENCE
{
    HANDLE hLocalNativeFence;
    DXGK_CLOSENATIVEFENCE_FLAGS Flags;
    BYTE Reserved[32];
} DXGKARG_CLOSENATIVEFENCE;

typedef _Inout_ DXGKARG_CLOSENATIVEFENCE
    *INOUT_PDXGKARG_CLOSENATIVEFENCE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CLOSENATIVEFENCE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_CLOSENATIVEFENCE(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CLOSENATIVEFENCE pCloseNativeFence);

typedef struct _DXGK_DESTROYNATIVEFENCE_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGK_DESTROYNATIVEFENCE_FLAGS;

typedef struct _DXGKARG_DESTROYNATIVEFENCE
{
    HANDLE hGlobalNativeFence;
    DXGK_DESTROYNATIVEFENCE_FLAGS Flags;
    BYTE Reserved[32];
} DXGKARG_DESTROYNATIVEFENCE;

typedef _Inout_ DXGKARG_DESTROYNATIVEFENCE
    *INOUT_PDXGKARG_DESTROYNATIVEFENCE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DESTROYNATIVEFENCE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_DESTROYNATIVEFENCE(
    INOUT_PDXGKARG_DESTROYNATIVEFENCE pDestroyNativeFence);

typedef struct _DXGKARG_UPDATEMONITOREDVALUES_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGKARG_UPDATEMONITOREDVALUES_FLAGS;

typedef struct _DXGKARG_UPDATEMONITOREDVALUES
{
    _Field_size_(NumFences) HANDLE *NativeFenceArray;
    _Field_size_(NumFences) UINT64 *UpdatedValueArray;
    _Field_size_(NumFences) VOID **MonitoredValueKernelCpuVa;
    UINT NumFences;
    DXGKARG_UPDATEMONITOREDVALUES_FLAGS Flags;
    BYTE Reserved[28];
} DXGKARG_UPDATEMONITOREDVALUES;

typedef _In_ CONST DXGKARG_UPDATEMONITOREDVALUES
    *IN_CONST_PDXGKARG_UPDATEMONITOREDVALUES;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_UPDATEMONITOREDVALUES)
    _IRQL_requires_(PROFILE_LEVEL - 1)
NTSTATUS
APIENTRY
DXGKDDI_UPDATEMONITOREDVALUES(
    IN_CONST_PDXGKARG_UPDATEMONITOREDVALUES pUpdateMonitoredValues);

typedef struct _DXGK_UPDATECURRENTVALUESFROMCPU_FLAGS
{
    union
    {
        struct
        {
            UINT AlwaysSignaled : 1;
            UINT NotificationOnly : 1;
            UINT Reserved : 30;
        };
        UINT Value;
    };
} DXGK_UPDATECURRENTVALUESFROMCPU_FLAGS;

typedef struct _DXGKARG_UPDATECURRENTVALUESFROMCPU
{
    _Field_size_(NumFences) HANDLE *NativeFenceArray;
    _Field_size_(NumFences) UINT64 *UpdatedValueArray;
    _Field_size_(NumFences) VOID **CurrentValueKernelCpuVa;
    UINT NumFences;
    DXGK_UPDATECURRENTVALUESFROMCPU_FLAGS Flags;
    BYTE Reserved[28];
} DXGKARG_UPDATECURRENTVALUESFROMCPU;

typedef _In_ CONST DXGKARG_UPDATECURRENTVALUESFROMCPU
    *IN_CONST_PDXGKARG_UPDATECURRENTVALUESFROMCPU;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_UPDATECURRENTVALUESFROMCPU)
    _IRQL_requires_(DISPATCH_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_UPDATECURRENTVALUESFROMCPU(
    IN_CONST_PDXGKARG_UPDATECURRENTVALUESFROMCPU pUpdateCurrentValuesFromCpu);

typedef struct _DXGKARG_SETNATIVEFENCELOGBUFFER_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGKARG_SETNATIVEFENCELOGBUFFER_FLAGS;

typedef struct _DXGKARG_SETNATIVEFENCELOGBUFFER
{
    HANDLE hHwQueue;
    UINT NumberOfEntries;
    _Field_size_bytes_(32 + 32 * NumberOfEntries)
    DXGK_NATIVE_FENCE_LOG_BUFFER *LogBufferCpuVa;
    D3DGPU_VIRTUAL_ADDRESS LogBufferGpuVa;
    D3DGPU_VIRTUAL_ADDRESS LogBufferSystemProcessGpuVa;
    DXGKARG_SETNATIVEFENCELOGBUFFER_FLAGS Flags;
    BYTE Reserved[32];
} DXGKARG_SETNATIVEFENCELOGBUFFER;

typedef _In_ CONST DXGKARG_SETNATIVEFENCELOGBUFFER
    *IN_CONST_PDXGKARG_SETNATIVEFENCELOGBUFFER;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETNATIVEFENCELOGBUFFER)
    _IRQL_requires_(DISPATCH_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETNATIVEFENCELOGBUFFER(
    IN_CONST_PDXGKARG_SETNATIVEFENCELOGBUFFER pSetNativeFenceLogBuffer);

typedef struct _DXGKARG_UPDATENATIVEFENCELOGS_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGKARG_UPDATENATIVEFENCELOGS_FLAGS;

typedef struct _DXGKARG_UPDATENATIVEFENCELOGS
{
    UINT NumberOfQueues;
    HANDLE *hHWQueue;
    DXGKARG_UPDATENATIVEFENCELOGS_FLAGS Flags;
    BYTE Reserved[32];
} DXGKARG_UPDATENATIVEFENCELOGS;

typedef _In_ CONST DXGKARG_UPDATENATIVEFENCELOGS
    *IN_CONST_PDXGKARG_UPDATENATIVEFENCELOGS;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_UPDATENATIVEFENCELOGS)
    _IRQL_requires_(DISPATCH_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_UPDATENATIVEFENCELOGS(
    IN_CONST_PDXGKARG_UPDATENATIVEFENCELOGS pUpdateNativeFenceLog);

#define DXGK_MAX_DOORBELL_SIZE_BYTES 16384

typedef struct _DXGK_USERMODESUBMISSION_CAPS
{
    union
    {
        struct
        {
            UINT SecondaryDoorbellSupported : 1;
            UINT Reserved : 31;
        };
        UINT Value;
    } Flags;
    UINT DoorbellSizeInBytes;
    UINT SecondaryDoorbellSizeInBytes;
    UCHAR Reserved[16];
} DXGK_USERMODESUBMISSION_CAPS;

typedef struct _DXGKARG_CREATEDOORBELL_FLAGS
{
    union
    {
        struct
        {
            UINT ResizeRingBufferOperation : 1;
            UINT Reserved : 31;
        };
        UINT Value;
    };
} DXGKARG_CREATEDOORBELL_FLAGS;

typedef struct _DXGKARG_CREATEDOORBELL
{
    HANDLE hHwQueue;
    HANDLE hDoorbell;
    _Field_range_(0, D3DDDI_DOORBELL_PRIVATEDATA_MAX_BYTES_WDDM3_1)
    UINT PrivateDriverDataSize;
    _Field_size_(PrivateDriverDataSize) VOID *PrivateDriverData;
    HANDLE hRingBuffer;
    HANDLE hRingBufferControl;
    DXGKARG_CREATEDOORBELL_FLAGS Flags;
} DXGKARG_CREATEDOORBELL;

typedef _Inout_ DXGKARG_CREATEDOORBELL *INOUT_PDXGKARG_CREATEDOORBELL;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CREATEDOORBELL)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_CREATEDOORBELL(
    INOUT_PDXGKARG_CREATEDOORBELL pArgs);

typedef struct _DXGKARG_CONNECTDOORBELL_FLAGS
{
    union
    {
        struct
        {
            UINT RequireSecondaryAddress : 1;
            UINT Reserved : 31;
        };
        UINT Value;
    };
} DXGKARG_CONNECTDOORBELL_FLAGS;

typedef struct _DXGKARG_CONNECTDOORBELL
{
    HANDLE hDoorbell;
    DXGKARG_CONNECTDOORBELL_FLAGS Flags;
    VOID *KernelCpuVirtualAddress;
    VOID *SecondaryKernelCpuVirtualAddress;
    D3DDDI_DOORBELLSTATUS Status;
} DXGKARG_CONNECTDOORBELL;

typedef _Inout_ DXGKARG_CONNECTDOORBELL *INOUT_PDXGKARG_CONNECTDOORBELL;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CONNECTDOORBELL)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_CONNECTDOORBELL(
    INOUT_PDXGKARG_CONNECTDOORBELL pArgs);

typedef struct _DXGKARG_DISCONNECTDOORBELL_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGKARG_DISCONNECTDOORBELL_FLAGS;

typedef struct _DXGKARG_DISCONNECTDOORBELL
{
    HANDLE hDoorbell;
    DXGKARG_DISCONNECTDOORBELL_FLAGS Flags;
} DXGKARG_DISCONNECTDOORBELL;

typedef _Inout_ DXGKARG_DISCONNECTDOORBELL
    *INOUT_PDXGKARG_DISCONNECTDOORBELL;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DISCONNECTDOORBELL)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_DISCONNECTDOORBELL(
    INOUT_PDXGKARG_DISCONNECTDOORBELL pArgs);

typedef struct _DXGKARG_DESTROYDOORBELL
{
    HANDLE hDoorbell;
} DXGKARG_DESTROYDOORBELL;

typedef _Inout_ DXGKARG_DESTROYDOORBELL
    *INOUT_PDXGKARG_DESTROYDOORBELL;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DESTROYDOORBELL)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_DESTROYDOORBELL(
    INOUT_PDXGKARG_DESTROYDOORBELL pArgs);

typedef struct _DXGKARG_NOTIFYWORKSUBMISSION_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGKARG_NOTIFYWORKSUBMISSION_FLAGS;

typedef struct _DXGKARG_NOTIFYWORKSUBMISSION
{
    HANDLE hHwQueue;
    DXGKARG_NOTIFYWORKSUBMISSION_FLAGS Flags;
} DXGKARG_NOTIFYWORKSUBMISSION;

typedef _Inout_ DXGKARG_NOTIFYWORKSUBMISSION
    *INOUT_PDXGKARG_NOTIFYWORKSUBMISSION;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_NOTIFYWORKSUBMISSION)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_NOTIFYWORKSUBMISSION(
    INOUT_PDXGKARG_NOTIFYWORKSUBMISSION pArgs);

typedef struct _DXGKARGCB_DISCONNECTDOORBELL_FLAGS
{
    union
    {
        struct
        {
            UINT Reserved : 32;
        };
        UINT Value;
    };
} DXGKARGCB_DISCONNECTDOORBELL_FLAGS;

typedef struct _DXGKARGCB_DISCONNECTDOORBELL
{
    HANDLE hHwQueue;
    HANDLE hDoorbell;
    DXGKARGCB_DISCONNECTDOORBELL_FLAGS Flags;
    D3DDDI_DOORBELLSTATUS DisconnectReason;
} DXGKARGCB_DISCONNECTDOORBELL;

typedef _Inout_ DXGKARGCB_DISCONNECTDOORBELL
    *INOUT_PDXGKARGCB_DISCONNECTDOORBELL;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKCB_DISCONNECTDOORBELL)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
(APIENTRY CALLBACK *DXGKCB_DISCONNECTDOORBELL)(
    INOUT_PDXGKARGCB_DISCONNECTDOORBELL pArgs);

typedef DXGKDDI_CREATENATIVEFENCE          *PDXGKDDI_CREATENATIVEFENCE;
typedef DXGKDDI_OPENNATIVEFENCE            *PDXGKDDI_OPENNATIVEFENCE;
typedef DXGKDDI_CLOSENATIVEFENCE           *PDXGKDDI_CLOSENATIVEFENCE;
typedef DXGKDDI_DESTROYNATIVEFENCE         *PDXGKDDI_DESTROYNATIVEFENCE;
typedef DXGKDDI_UPDATEMONITOREDVALUES      *PDXGKDDI_UPDATEMONITOREDVALUES;
typedef DXGKDDI_UPDATECURRENTVALUESFROMCPU *PDXGKDDI_UPDATECURRENTVALUESFROMCPU;
typedef DXGKDDI_SETNATIVEFENCELOGBUFFER    *PDXGKDDI_SETNATIVEFENCELOGBUFFER;
typedef DXGKDDI_UPDATENATIVEFENCELOGS      *PDXGKDDI_UPDATENATIVEFENCELOGS;
typedef DXGKDDI_CREATEDOORBELL             *PDXGKDDI_CREATEDOORBELL;
typedef DXGKDDI_CONNECTDOORBELL            *PDXGKDDI_CONNECTDOORBELL;
typedef DXGKDDI_DISCONNECTDOORBELL         *PDXGKDDI_DISCONNECTDOORBELL;
typedef DXGKDDI_DESTROYDOORBELL            *PDXGKDDI_DESTROYDOORBELL;
typedef DXGKDDI_NOTIFYWORKSUBMISSION       *PDXGKDDI_NOTIFYWORKSUBMISSION;

C_ASSERT(sizeof(D3DDDI_NATIVEFENCEMAPPING) == 0x38);
C_ASSERT(sizeof(D3DDDI_NATIVEFENCEINFO) == 0x70);
C_ASSERT(sizeof(DXGK_NATIVE_FENCE_LOG_HEADER) == 0x28);
C_ASSERT(sizeof(DXGK_NATIVE_FENCE_LOG_ENTRY) == 0x30);
C_ASSERT(sizeof(DXGK_NATIVE_FENCE_LOG_BUFFER) == 0x58);
C_ASSERT(sizeof(DXGK_NATIVE_FENCE_CAPS) == 0x38);
C_ASSERT(sizeof(DXGK_USERMODESUBMISSION_CAPS) == 0x1C);
#ifdef _WIN64
C_ASSERT(sizeof(DXGKARG_CREATENATIVEFENCE) == 0x88);
C_ASSERT(sizeof(DXGKARG_OPENNATIVEFENCE) == 0x50);
C_ASSERT(sizeof(DXGKARG_CLOSENATIVEFENCE) == 0x30);
C_ASSERT(sizeof(DXGKARG_DESTROYNATIVEFENCE) == 0x30);
C_ASSERT(sizeof(DXGKARG_UPDATEMONITOREDVALUES) == 0x40);
C_ASSERT(sizeof(DXGKARG_UPDATECURRENTVALUESFROMCPU) == 0x40);
C_ASSERT(sizeof(DXGKARG_SETNATIVEFENCELOGBUFFER) == 0x50);
C_ASSERT(sizeof(DXGKARG_UPDATENATIVEFENCELOGS) == 0x38);
C_ASSERT(sizeof(DXGKARG_CREATEDOORBELL) == 0x38);
C_ASSERT(sizeof(DXGKARG_CONNECTDOORBELL) == 0x28);
C_ASSERT(sizeof(DXGKARG_DISCONNECTDOORBELL) == 0x10);
C_ASSERT(sizeof(DXGKARG_DESTROYDOORBELL) == 0x8);
C_ASSERT(sizeof(DXGKARG_NOTIFYWORKSUBMISSION) == 0x10);
C_ASSERT(sizeof(DXGKARGCB_DISCONNECTDOORBELL) == 0x18);
#else
C_ASSERT(sizeof(DXGKARG_CREATENATIVEFENCE) == 0x80);
C_ASSERT(sizeof(DXGKARG_OPENNATIVEFENCE) == 0x48);
C_ASSERT(sizeof(DXGKARG_CLOSENATIVEFENCE) == 0x28);
C_ASSERT(sizeof(DXGKARG_DESTROYNATIVEFENCE) == 0x28);
C_ASSERT(sizeof(DXGKARG_UPDATEMONITOREDVALUES) == 0x30);
C_ASSERT(sizeof(DXGKARG_UPDATECURRENTVALUESFROMCPU) == 0x30);
C_ASSERT(sizeof(DXGKARG_SETNATIVEFENCELOGBUFFER) == 0x48);
C_ASSERT(sizeof(DXGKARG_UPDATENATIVEFENCELOGS) == 0x2C);
C_ASSERT(sizeof(DXGKARG_CREATEDOORBELL) == 0x1C);
C_ASSERT(sizeof(DXGKARG_CONNECTDOORBELL) == 0x14);
C_ASSERT(sizeof(DXGKARG_DISCONNECTDOORBELL) == 0x8);
C_ASSERT(sizeof(DXGKARG_DESTROYDOORBELL) == 0x4);
C_ASSERT(sizeof(DXGKARG_NOTIFYWORKSUBMISSION) == 0x8);
C_ASSERT(sizeof(DXGKARGCB_DISCONNECTDOORBELL) == 0x10);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_1 */

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)

typedef struct _DXGK_DIRTY_BIT_TRACKING_CAPS
{
    union
    {
        struct
        {
            UINT32 DirtyBitTrackingSupported  : 1;
            UINT32 DirtyBitTrackingPerformant : 1;
            UINT32 Reserved                   : 30;
        };
        UINT32 Value;
    };
} DXGK_DIRTY_BIT_TRACKING_CAPS;

typedef struct _DXGK_DIRTY_BIT_TRACKING_SEGMENT_CAPS
{
    UINT32 PageSize;
} DXGK_DIRTY_BIT_TRACKING_SEGMENT_CAPS;

typedef struct _DXGKARG_CREATEMEMORYBASIS
{
    UINT SegmentId;
    UINT64 RangeCount;
    _Field_size_(RangeCount)
    DXGK_MEMORYRANGE Ranges[1];
} DXGKARG_CREATEMEMORYBASIS;

typedef _In_ CONST DXGKARG_CREATEMEMORYBASIS
    *IN_CONST_PDXGKARG_CREATEMEMORYBASIS;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_CREATEMEMORYBASIS)
    _IRQL_requires_(PASSIVE_LEVEL)
HANDLE
APIENTRY
DXGKDDI_CREATEMEMORYBASIS(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_CREATEMEMORYBASIS pArgs);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_DESTROYMEMORYBASIS)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_DESTROYMEMORYBASIS(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hMemoryBasis);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_STARTDIRTYTRACKING)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_STARTDIRTYTRACKING(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hMemoryBasis);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_STOPDIRTYTRACKING)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_STOPDIRTYTRACKING(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hMemoryBasis);

typedef enum _DXGKARG_QUERYDIRTYBITDATAFLAGS
{
    DXGKARG_QUERYDIRTYBITDATAFLAGS_CLEARDATA = 1,
} DXGKARG_QUERYDIRTYBITDATAFLAGS;

typedef struct _DXGKARG_QUERYDIRTYBITDATA
{
    HANDLE MemoryBasis;
    UINT64 SubrangeIndex;
    UINT64 SubrangeOffset;
    UINT64 SubrangeSize;
    _Field_size_bytes_(BufferSize)
    PVOID Buffer;
    SIZE_T BufferSize;
    UINT Flags;
} DXGKARG_QUERYDIRTYBITDATA;

typedef _Inout_ DXGKARG_QUERYDIRTYBITDATA
    *INOUT_PDXGKARG_QUERYDIRTYBITDATA;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_QUERYDIRTYBITDATA)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_QUERYDIRTYBITDATA(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_QUERYDIRTYBITDATA pArgs);

typedef struct _DXGK_QUERYSCATTERRESERVEIN
{
    UINT SegmentId;
} DXGK_QUERYSCATTERRESERVEIN;

typedef struct _DXGK_QUERYSCATTERRESERVEOUT
{
    UINT64 SetVGPUResourcesPageSize;
} DXGK_QUERYSCATTERRESERVEOUT;

typedef enum _DXGK_GPUP_MIGRATIONTYPE
{
    DXGK_GPUP_MIGRATIONTYPE_SOURCE = 0,
    DXGK_GPUP_MIGRATIONTYPE_TARGET
} DXGK_GPUP_MIGRATIONTYPE;

typedef struct _DXGKARG_GPUP_PREPARE_LIVE_MIGRATION
{
    UINT vfIndex;
    DXGK_GPUP_MIGRATIONTYPE MigrationType;
} DXGKARG_GPUP_PREPARE_LIVE_MIGRATION;

typedef _In_ CONST DXGKARG_GPUP_PREPARE_LIVE_MIGRATION
    *IN_CONST_PDXGKARG_GPUP_PREPARE_LIVE_MIGRATION;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_PREPARELIVEMIGRATION)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_PREPARELIVEMIGRATION(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_GPUP_PREPARE_LIVE_MIGRATION pArgs);

typedef struct _DXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA
{
    UINT vfIndex;
    UINT64 *DataSize;
    BYTE *Data;
} DXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA;

typedef _Inout_ DXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA
    *INOUT_PDXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SAVEIMMUTABLEMIGRATIONDATA)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SAVEIMMUTABLEMIGRATIONDATA(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA pArgs);

typedef struct _DXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA
{
    UINT vfIndex;
    UINT64 *DataSize;
    BYTE *Data;
} DXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA;

typedef _Inout_ DXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA
    *INOUT_PDXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SAVEMUTABLEMIGRATIONDATA)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SAVEMUTABLEMIGRATIONDATA(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA pArgs);

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_ENDLIVEMIGRATION)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_ENDLIVEMIGRATION(
    IN_CONST_HANDLE hAdapter,
    UINT vfIndex);

typedef struct _DXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA
{
    UINT vfIndex;
    UINT64 DataSize;
    BYTE *Data;
} DXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA;

typedef _In_ CONST DXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA
    *IN_CONST_PDXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_RESTOREIMMUTABLEMIGRATIONDATA)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_RESTOREIMMUTABLEMIGRATIONDATA(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA pArgs);

typedef struct _DXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA
{
    UINT vfIndex;
    UINT64 DataSize;
    BYTE *Data;
} DXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA;

typedef _In_ CONST DXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA
    *IN_CONST_PDXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_RESTOREMUTABLEMIGRATIONDATA)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_RESTOREMUTABLEMIGRATIONDATA(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA pArgs);

typedef struct _DXGK_INTERRUPT_TABLE_ENTRY
{
    UINT64 MessageAddress;
    UINT32 MessageData;
    UINT32 VectorControl;
} DXGK_INTERRUPT_TABLE_ENTRY;

typedef struct _DXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX
{
    UINT vfIndex;
    INT16 InterruptTableIndex;
    DXGK_INTERRUPT_TABLE_ENTRY WriteValue;
} DXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX;

typedef _In_ CONST DXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX
    *IN_CONST_PDXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_WRITEVIRTUALIZEDINTERRUPT)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_WRITEVIRTUALIZEDINTERRUPT(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX pArgs);

typedef struct _DXGK_GPU_PHYSICAL_RESERVE_DESCRIPTOR
{
    HANDLE DriverAllocationHandle;
    HANDLE MemoryBasis;
} DXGK_GPU_PHYSICAL_RESERVE_DESCRIPTOR;

typedef struct _DXGKARG_SETVIRTUALGPURESOURCES2
{
    ULONG vfIndex;
    ULONG SegmentCount;
    _Field_size_(SegmentCount)
    DXGK_GPU_PHYSICAL_RESERVE_DESCRIPTOR SegmentDescriptors[1];
} DXGKARG_SETVIRTUALGPURESOURCES2;

typedef _In_ CONST DXGKARG_SETVIRTUALGPURESOURCES2
    *IN_CONST_PDXGKARG_SETVIRTUALGPURESOURCES2;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETVIRTUALGPURESOURCES2)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETVIRTUALGPURESOURCES2(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALGPURESOURCES2 pArgs);

typedef struct _DXGKARG_SETVIRTUALFUNCTIONPAUSESTATE
{
    ULONG vfIndex;
    BOOLEAN bPause;
} DXGKARG_SETVIRTUALFUNCTIONPAUSESTATE;

typedef _In_ CONST DXGKARG_SETVIRTUALFUNCTIONPAUSESTATE
    *IN_CONST_PDXGKARG_SETVIRTUALFUNCTIONPAUSESTATE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_SETVIRTUALFUNCTIONPAUSESTATE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_SETVIRTUALFUNCTIONPAUSESTATE(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIRTUALFUNCTIONPAUSESTATE pArgs);

typedef enum _DXGK_TDR_TYPE
{
    DXGK_TDR_TYPE_UNKNOWN = 0,
    DXGK_TDR_TYPE_FORCED = 1,
    DXGK_TDR_TYPE_PREEMPT_TIMEOUT = 2,
    DXGK_TDR_TYPE_VSYNC_TIMEOUT = 3,
    DXGK_TDR_TYPE_DOD_PRESENT_FORCED = 4,
    DXGK_TDR_TYPE_DOD_PRESENT_TIMEOUT = 5,
    DXGK_TDR_TYPE_ENGINE_TIMEOUT = 6,
    DXGK_TDR_TYPE_DOD_VSYNC_FORCED = 7,
    DXGK_TDR_TYPE_DOD_VSYNC_TIMEOUT = 8,
    DXGK_TDR_TYPE_ENGINE_TIMEOUT_PROMOTED = 9,
    DXGK_TDR_TYPE_PAGE_FAULT = 10,
    DXGK_TDR_TYPE_INVALID_FENCE = 11,
    DXGK_TDR_TYPE_ENGINE_PAGE_FAULT = 12,
    DXGK_TDR_TYPE_DISPLAY_ENGINE_FAULT = 13,
} DXGK_TDR_TYPE;

typedef struct _DXGK_TDR_PAYLOAD_ENGINE_TIMEOUT
{
    UINT NodeOrdinal;
    UINT EngineOrdinal;
    ULONGLONG LastHwCompletedFenceId;
    ULONGLONG LastHwSubmittedFenceId;
    ULONG NumberOfPendingSuspendRequests;
    ULONG NumberOfReadyInteractiveHwQueues;
    HANDLE hContext;
} DXGK_TDR_PAYLOAD_ENGINE_TIMEOUT;

typedef struct _DXGK_TDR_PAYLOAD_VSYNC_TIMEOUT
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT LayerIndex;
    ULONGLONG PresentId;
} DXGK_TDR_PAYLOAD_VSYNC_TIMEOUT;

typedef struct _DXGKARG_COLLECTDBGINFO2
{
    UINT Reason;
    VOID *pBuffer;
    SIZE_T BufferSize;
    DXGKARG_COLLECTDBGINFO_EXT *pExtension;
    DXGK_TDR_TYPE TdrType;
    UINT TdrPayloadSize;
    _Field_size_opt_(TdrPayloadSize)
    VOID *TdrPayload;
} DXGKARG_COLLECTDBGINFO2;

typedef _Inout_ DXGKARG_COLLECTDBGINFO2
    *INOUT_PDXGKARG_COLLECTDBGINFO2;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_COLLECTDBGINFO2)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_COLLECTDBGINFO2(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_COLLECTDBGINFO2 pCollectDbgInfo2);

typedef struct _DXGK_QUERYPAGINGBUFFERINFOIN
{
    UINT16 PhysicalAdapterIndex;
    UINT16 Reserved;
} DXGK_QUERYPAGINGBUFFERINFOIN;

typedef struct _DXGK_QUERYPAGINGBUFFERINFOOUT
{
    UINT32 PagingBufferSize;
    UINT32 PagingBufferPrivateDataSize;
} DXGK_QUERYPAGINGBUFFERINFOOUT;

typedef struct _DXGK_QUERYSEGMENTCOUNTIN
{
    UINT16 PhysicalAdapterIndex;
    UINT16 Padding;
    UINT32 Reserved;
} DXGK_QUERYSEGMENTCOUNTIN;

typedef struct _DXGK_QUERYSEGMENTCOUNTOUT
{
    UINT16 SegmentCount;
    UINT16 Padding;
    UINT32 Reserved;
} DXGK_QUERYSEGMENTCOUNTOUT;

typedef enum _DXGK_SEGMENTTYPE
{
    DXGK_SEGMENTTYPE_SYSMEM,
    DXGK_SEGMENTTYPE_LOCAL,
} DXGK_SEGMENTTYPE;

typedef enum _DXGK_PAGESIZE
{
    DXGK_PAGESIZE_4KB = 0,
    DXGK_PAGESIZE_8KB = 1,
    DXGK_PAGESIZE_16KB = 2,
    DXGK_PAGESIZE_32KB = 3,
    DXGK_PAGESIZE_64KB = 4,
    DXGK_PAGESIZE_128KB = 5,
    DXGK_PAGESIZE_256KB = 6,
    DXGK_PAGESIZE_512KB = 7,
    DXGK_PAGESIZE_1MB = 8,
    DXGK_PAGESIZE_2MB = 9,
    DXGK_PAGESIZE_4MB = 10,
    DXGK_PAGESIZE_8MB = 11,
    DXGK_PAGESIZE_16MB = 12,
    DXGK_PAGESIZE_32MB = 13,
    DXGK_PAGESIZE_64MB = 14,
    DXGK_PAGESIZE_128MB = 15
} DXGK_PAGESIZE;

typedef struct _DXGK_SEGMENTDESCRIPTOR5
{
    DXGK_SEGMENTTYPE SegmentType;
    DXGK_SEGMENTFLAGS Flags;
    PHYSICAL_ADDRESS BaseAddress;
    UINT64 Size;
    SIZE_T SystemMemoryEndAddress;
    union
    {
        PHYSICAL_ADDRESS CpuTranslatedAddress;
        DXGK_CPUHOSTAPERTURE CpuHostAperture;
    };
    SIZE_T VprRangeStartOffset;
    SIZE_T VprRangeSize;
    UINT32 VprAlignment;
    UINT32 NumInvalidMemoryRanges;
    UINT32 NumVprSupported;
    UINT32 VprReserveSize;
    UINT32 NumUEFIFrameBufferRanges;
    DXGK_PAGESIZE SlabSize;
} DXGK_SEGMENTDESCRIPTOR5;

typedef struct _DXGK_QUERYSEGMENTIN5
{
    UINT16 PhysicalAdapterIndex;
    UINT16 Padding;
    UINT32 Reserved;
} DXGK_QUERYSEGMENTIN5;

typedef struct _DXGK_QUERYSEGMENTOUT5
{
    DXGK_SEGMENTDESCRIPTOR5 *SegmentDescriptors;
    UINT32 Reserved[4];
} DXGK_QUERYSEGMENTOUT5;

#define DXGK_MAX_MMUS 16

typedef struct _DXGK_QUERYMMUCOUNTIN
{
    UINT16 PhysicalAdapterIndex;
    UINT16 Padding;
    UINT32 Reserved;
} DXGK_QUERYMMUCOUNTIN;

typedef struct _DXGK_QUERYMMUCOUNTOUT
{
    UINT16 MmuCount;
    UINT16 Padding;
    UINT32 Reserved;
} DXGK_QUERYMMUCOUNTOUT;

#define DXGK_INVALID_MMU_ID 0xFFFF

typedef union _DXGK_MMUFLAGS
{
    struct
    {
        UINT32 Reserved : 32;
    };
    UINT32 Value;
} DXGK_MMUFLAGS;

typedef struct _DXGK_MMUDESCRIPTOR
{
    DXGK_MMUFLAGS Flags;
    UINT64 Size;
} DXGK_MMUDESCRIPTOR;

typedef struct _DXGK_QUERYMMUSIN
{
    UINT16 PhysicalAdapterIndex;
} DXGK_QUERYMMUSIN;

typedef struct _DXGK_QUERYMMUSOUT
{
    DXGK_MMUDESCRIPTOR *MmuDescriptors;
    UINT16 DisplayMmuId;
    UINT16 Reserved0;
    UINT32 Reserved[4];
} DXGK_QUERYMMUSOUT;

typedef struct _DXGKARG_NOTIFYCONTEXTPRIORITYCHANGE
{
    HANDLE hContext;
    INT SchedulingPriority;
    INT InProcessPriority;
} DXGKARG_NOTIFYCONTEXTPRIORITYCHANGE;

typedef _In_ CONST DXGKARG_NOTIFYCONTEXTPRIORITYCHANGE
    *IN_CONST_PDXGKARG_NOTIFYCONTEXTPRIORITYCHANGE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_NOTIFYCONTEXTPRIORITYCHANGE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_NOTIFYCONTEXTPRIORITYCHANGE(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_NOTIFYCONTEXTPRIORITYCHANGE pNotifyContextPriorityChange);

typedef struct _DXGKARG_RESETDISPLAYENGINE
{
    UINT64 InputFlags;
    UINT64 OutputFlags;
} DXGKARG_RESETDISPLAYENGINE;

typedef _Inout_ DXGKARG_RESETDISPLAYENGINE
    *INOUT_PDXGKARG_RESETDISPLAYENGINE;

typedef
    _Check_return_
    _Function_class_DXGK_(DXGKDDI_RESETDISPLAYENGINE)
    _IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
APIENTRY
DXGKDDI_RESETDISPLAYENGINE(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_RESETDISPLAYENGINE pResetDisplayEngine);

typedef DXGKDDI_CREATEMEMORYBASIS             *PDXGKDDI_CREATEMEMORYBASIS;
typedef DXGKDDI_DESTROYMEMORYBASIS            *PDXGKDDI_DESTROYMEMORYBASIS;
typedef DXGKDDI_STARTDIRTYTRACKING            *PDXGKDDI_STARTDIRTYTRACKING;
typedef DXGKDDI_STOPDIRTYTRACKING             *PDXGKDDI_STOPDIRTYTRACKING;
typedef DXGKDDI_QUERYDIRTYBITDATA             *PDXGKDDI_QUERYDIRTYBITDATA;
typedef DXGKDDI_PREPARELIVEMIGRATION          *PDXGKDDI_PREPARELIVEMIGRATION;
typedef DXGKDDI_SAVEIMMUTABLEMIGRATIONDATA    *PDXGKDDI_SAVEIMMUTABLEMIGRATIONDATA;
typedef DXGKDDI_SAVEMUTABLEMIGRATIONDATA      *PDXGKDDI_SAVEMUTABLEMIGRATIONDATA;
typedef DXGKDDI_ENDLIVEMIGRATION              *PDXGKDDI_ENDLIVEMIGRATION;
typedef DXGKDDI_RESTOREIMMUTABLEMIGRATIONDATA *PDXGKDDI_RESTOREIMMUTABLEMIGRATIONDATA;
typedef DXGKDDI_RESTOREMUTABLEMIGRATIONDATA   *PDXGKDDI_RESTOREMUTABLEMIGRATIONDATA;
typedef DXGKDDI_WRITEVIRTUALIZEDINTERRUPT     *PDXGKDDI_WRITEVIRTUALIZEDINTERRUPT;
typedef DXGKDDI_SETVIRTUALGPURESOURCES2       *PDXGKDDI_SETVIRTUALGPURESOURCES2;
typedef DXGKDDI_SETVIRTUALFUNCTIONPAUSESTATE  *PDXGKDDI_SETVIRTUALFUNCTIONPAUSESTATE;
typedef DXGKDDI_COLLECTDBGINFO2               *PDXGKDDI_COLLECTDBGINFO2;
typedef DXGKDDI_NOTIFYCONTEXTPRIORITYCHANGE   *PDXGKDDI_NOTIFYCONTEXTPRIORITYCHANGE;
typedef DXGKDDI_RESETDISPLAYENGINE            *PDXGKDDI_RESETDISPLAYENGINE;

C_ASSERT(sizeof(DXGK_DIRTY_BIT_TRACKING_CAPS) == 0x4);
C_ASSERT(sizeof(DXGK_DIRTY_BIT_TRACKING_SEGMENT_CAPS) == 0x4);
C_ASSERT(sizeof(DXGKARG_CREATEMEMORYBASIS) == 0x20);
C_ASSERT(sizeof(DXGK_QUERYSCATTERRESERVEIN) == 0x4);
C_ASSERT(sizeof(DXGK_QUERYSCATTERRESERVEOUT) == 0x8);
C_ASSERT(sizeof(DXGKARG_GPUP_PREPARE_LIVE_MIGRATION) == 0x8);
C_ASSERT(sizeof(DXGK_INTERRUPT_TABLE_ENTRY) == 0x10);
C_ASSERT(sizeof(DXGKARG_GPUP_WRITE_VIRTUALIZED_MSIX) == 0x18);
C_ASSERT(sizeof(DXGKARG_SETVIRTUALFUNCTIONPAUSESTATE) == 0x8);
C_ASSERT(sizeof(DXGK_TDR_PAYLOAD_VSYNC_TIMEOUT) == 0x10);
C_ASSERT(sizeof(DXGK_QUERYPAGINGBUFFERINFOIN) == 0x4);
C_ASSERT(sizeof(DXGK_QUERYPAGINGBUFFERINFOOUT) == 0x8);
C_ASSERT(sizeof(DXGK_QUERYSEGMENTCOUNTIN) == 0x8);
C_ASSERT(sizeof(DXGK_QUERYSEGMENTCOUNTOUT) == 0x8);
C_ASSERT(sizeof(DXGK_QUERYSEGMENTIN5) == 0x8);
C_ASSERT(sizeof(DXGK_QUERYMMUCOUNTIN) == 0x8);
C_ASSERT(sizeof(DXGK_QUERYMMUCOUNTOUT) == 0x8);
C_ASSERT(sizeof(DXGK_MMUDESCRIPTOR) == 0x10);
C_ASSERT(sizeof(DXGK_QUERYMMUSIN) == 0x2);
C_ASSERT(sizeof(DXGKARG_RESETDISPLAYENGINE) == 0x10);
#ifdef _WIN64
C_ASSERT(sizeof(DXGKARG_QUERYDIRTYBITDATA) == 0x38);
C_ASSERT(sizeof(DXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA) == 0x18);
C_ASSERT(sizeof(DXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA) == 0x18);
C_ASSERT(sizeof(DXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA) == 0x18);
C_ASSERT(sizeof(DXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA) == 0x18);
C_ASSERT(sizeof(DXGK_GPU_PHYSICAL_RESERVE_DESCRIPTOR) == 0x10);
C_ASSERT(sizeof(DXGKARG_SETVIRTUALGPURESOURCES2) == 0x18);
C_ASSERT(sizeof(DXGK_TDR_PAYLOAD_ENGINE_TIMEOUT) == 0x28);
C_ASSERT(sizeof(DXGKARG_COLLECTDBGINFO2) == 0x30);
C_ASSERT(sizeof(DXGK_SEGMENTDESCRIPTOR5) == 0x58);
C_ASSERT(sizeof(DXGK_QUERYSEGMENTOUT5) == 0x18);
C_ASSERT(sizeof(DXGK_QUERYMMUSOUT) == 0x20);
C_ASSERT(sizeof(DXGKARG_NOTIFYCONTEXTPRIORITYCHANGE) == 0x10);
#else
C_ASSERT(sizeof(DXGKARG_QUERYDIRTYBITDATA) == 0x30);
C_ASSERT(sizeof(DXGKARG_GPUP_SAVE_IMMUTABLE_MIGRATION_DATA) == 0xC);
C_ASSERT(sizeof(DXGKARG_GPUP_SAVE_MUTABLE_MIGRATION_DATA) == 0xC);
C_ASSERT(sizeof(DXGKARG_GPUP_RESTORE_IMMUTABLE_MIGRATION_DATA) == 0x18);
C_ASSERT(sizeof(DXGKARG_GPUP_RESTORE_MUTABLE_MIGRATION_DATA) == 0x18);
C_ASSERT(sizeof(DXGK_GPU_PHYSICAL_RESERVE_DESCRIPTOR) == 0x8);
C_ASSERT(sizeof(DXGKARG_SETVIRTUALGPURESOURCES2) == 0x10);
C_ASSERT(sizeof(DXGK_TDR_PAYLOAD_ENGINE_TIMEOUT) == 0x28);
C_ASSERT(sizeof(DXGKARG_COLLECTDBGINFO2) == 0x1C);
C_ASSERT(sizeof(DXGK_SEGMENTDESCRIPTOR5) == 0x50);
C_ASSERT(sizeof(DXGK_QUERYSEGMENTOUT5) == 0x14);
C_ASSERT(sizeof(DXGK_QUERYMMUSOUT) == 0x18);
C_ASSERT(sizeof(DXGKARG_NOTIFYCONTEXTPRIORITYCHANGE) == 0xC);
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM3_2 */

typedef struct _DXGK_64_BIT_ONLY_CAPS
{
    union
    {
        struct
        {
            UINT32 SupportsOnly64Bit : 1;
            UINT32 Reserved          : 31;
        };
        UINT32 Value;
    };
} DXGK_64_BIT_ONLY_CAPS;

C_ASSERT(sizeof(DXGK_64_BIT_ONLY_CAPS) == 0x4);

typedef DXGKDDI_CREATEHWCONTEXT                    *PDXGKDDI_CREATEHWCONTEXT;
typedef DXGKDDI_DESTROYHWCONTEXT                   *PDXGKDDI_DESTROYHWCONTEXT;
typedef DXGKDDI_CREATEHWQUEUE                      *PDXGKDDI_CREATEHWQUEUE;
typedef DXGKDDI_DESTROYHWQUEUE                     *PDXGKDDI_DESTROYHWQUEUE;
typedef DXGKDDI_SUBMITCOMMANDTOHWQUEUE             *PDXGKDDI_SUBMITCOMMANDTOHWQUEUE;
typedef DXGKDDI_SWITCHTOHWCONTEXTLIST              *PDXGKDDI_SWITCHTOHWCONTEXTLIST;
typedef DXGKDDI_RESETHWENGINE                      *PDXGKDDI_RESETHWENGINE;
typedef DXGKDDI_CREATEPERIODICFRAMENOTIFICATION    *PDXGKDDI_CREATEPERIODICFRAMENOTIFICATION;
typedef DXGKDDI_DESTROYPERIODICFRAMENOTIFICATION   *PDXGKDDI_DESTROYPERIODICFRAMENOTIFICATION;
typedef DXGKDDI_SETTIMINGSFROMVIDPN                *PDXGKDDI_SETTIMINGSFROMVIDPN;
typedef DXGKDDI_SETTARGETGAMMA                     *PDXGKDDI_SETTARGETGAMMA;
typedef DXGKDDI_SETTARGETCONTENTTYPE               *PDXGKDDI_SETTARGETCONTENTTYPE;
typedef DXGKDDI_SETTARGETANALOGCOPYPROTECTION      *PDXGKDDI_SETTARGETANALOGCOPYPROTECTION;
typedef DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY       *PDXGKDDI_SETTARGETADJUSTEDCOLORIMETRY;
typedef DXGKDDI_DISPLAYDETECTCONTROL               *PDXGKDDI_DISPLAYDETECTCONTROL;
typedef DXGKDDI_QUERYCONNECTIONCHANGE              *PDXGKDDI_QUERYCONNECTIONCHANGE;
typedef DXGKDDI_EXCHANGEPRESTARTINFO               *PDXGKDDI_EXCHANGEPRESTARTINFO;
typedef DXGKDDI_GETMULTIPLANEOVERLAYCAPS           *PDXGKDDI_GETMULTIPLANEOVERLAYCAPS;
typedef DXGKDDI_GETPOSTCOMPOSITIONCAPS             *PDXGKDDI_GETPOSTCOMPOSITIONCAPS;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
typedef DXGKDDI_UPDATEHWCONTEXTSTATE                *PDXGKDDI_UPDATEHWCONTEXTSTATE;
typedef DXGKDDI_CREATEPROTECTEDSESSION              *PDXGKDDI_CREATEPROTECTEDSESSION;
typedef DXGKDDI_DESTROYPROTECTEDSESSION             *PDXGKDDI_DESTROYPROTECTEDSESSION;
#endif

#if (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_3)
C_ASSERT(sizeof(DXGK_HWCONTEXT_CAPS) == 0x4);
C_ASSERT(sizeof(DXGK_MULTIPLANEOVERLAYCAPS) == 0x4);
C_ASSERT(sizeof(DXGK_COLORIMETRY) == 0x34);
C_ASSERT(sizeof(DXGK_CONNECTION_CHANGE) == 0x18);
C_ASSERT(sizeof(DXGK_SET_TIMING_PATH_INFO) == 0x38);
C_ASSERT(sizeof(DXGK_INHERITED_TIMING_INFO) == 0xC);
C_ASSERT(sizeof(DXGKARG_RESETHWENGINE) == 0x8);
C_ASSERT(sizeof(DXGKARG_SETTARGETCONTENTTYPE) == 0x8);
C_ASSERT(sizeof(DXGKARG_SETTARGETANALOGCOPYPROTECTION) == 0x10);
C_ASSERT(sizeof(DXGKARG_DISPLAYDETECTCONTROL) == 0x4);
C_ASSERT(sizeof(DXGKARG_QUERYCONNECTIONCHANGE) == 0x18);
C_ASSERT(sizeof(DXGK_PRE_START_INFO) == 0x8);
C_ASSERT(sizeof(DXGKARG_GETMULTIPLANEOVERLAYCAPS) == 0x1C);
C_ASSERT(sizeof(DXGKARG_GETPOSTCOMPOSITIONCAPS) == 0xC);
#ifdef _WIN64
C_ASSERT(sizeof(DXGKARG_CREATEHWCONTEXT) == 0x28);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEHWCONTEXT, pPrivateDriverData) == 0x18);
C_ASSERT(sizeof(DXGKARG_CREATEHWQUEUE) == 0x30);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEHWQUEUE, HwQueueProgressFenceCPUVirtualAddress) == 0x20);
C_ASSERT(sizeof(DXGKARG_SUBMITCOMMANDTOHWQUEUE) == 0x40);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMANDTOHWQUEUE, HwQueueProgressFenceGpuVa) == 0x30);
C_ASSERT(sizeof(DXGKARG_SWITCHTOHWCONTEXTLIST) == 0x18);
C_ASSERT(sizeof(DXGKARG_CREATEPERIODICFRAMENOTIFICATION) == 0x28);
C_ASSERT(sizeof(DXGKARG_DESTROYPERIODICFRAMENOTIFICATION) == 0x10);
C_ASSERT(sizeof(DXGKARG_SETTIMINGSFROMVIDPN) == 0x28);
C_ASSERT(sizeof(DXGKARG_SETTARGETGAMMA) == 0x20);
#else
C_ASSERT(sizeof(DXGKARG_CREATEHWCONTEXT) == 0x1C);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEHWCONTEXT, pPrivateDriverData) == 0x14);
C_ASSERT(sizeof(DXGKARG_CREATEHWQUEUE) == 0x20);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEHWQUEUE, HwQueueProgressFenceCPUVirtualAddress) == 0x14);
C_ASSERT(sizeof(DXGKARG_SUBMITCOMMANDTOHWQUEUE) == 0x38);
C_ASSERT(FIELD_OFFSET(DXGKARG_SUBMITCOMMANDTOHWQUEUE, HwQueueProgressFenceGpuVa) == 0x28);
C_ASSERT(sizeof(DXGKARG_SWITCHTOHWCONTEXTLIST) == 0x10);
C_ASSERT(sizeof(DXGKARG_CREATEPERIODICFRAMENOTIFICATION) == 0x18);
C_ASSERT(sizeof(DXGKARG_DESTROYPERIODICFRAMENOTIFICATION) == 0x8);
C_ASSERT(sizeof(DXGKARG_SETTIMINGSFROMVIDPN) == 0x14);
C_ASSERT(sizeof(DXGKARG_SETTARGETGAMMA) == 0x10);
#endif
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
C_ASSERT(sizeof(DXGK_UPDATEHWCONTEXTSTATE_FLAGS) == 0x4);
C_ASSERT(sizeof(DXGKARG_UPDATEHWCONTEXTSTATE) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARG_UPDATEHWCONTEXTSTATE, ContextSwitchFence) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_UPDATEHWCONTEXTSTATE, Flags) == 0x14);
#ifdef _WIN64
C_ASSERT(sizeof(DXGKARG_CREATEPROTECTEDSESSION) == 0x18);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEPROTECTEDSESSION, pPrivateDriverData) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEPROTECTEDSESSION, PrivateDriverDataSize) == 0x10);
C_ASSERT(sizeof(DXGKARGCB_PROTECTEDSESSIONSTATUS) == 0x10);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_PROTECTEDSESSIONSTATUS, Status) == 0x8);
#else
C_ASSERT(sizeof(DXGKARG_CREATEPROTECTEDSESSION) == 0xC);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEPROTECTEDSESSION, pPrivateDriverData) == 0x4);
C_ASSERT(FIELD_OFFSET(DXGKARG_CREATEPROTECTEDSESSION, PrivateDriverDataSize) == 0x8);
C_ASSERT(sizeof(DXGKARGCB_PROTECTEDSESSIONSTATUS) == 0x8);
C_ASSERT(FIELD_OFFSET(DXGKARGCB_PROTECTEDSESSIONSTATUS, Status) == 0x4);
#endif
#endif

#endif /* DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2 */

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
