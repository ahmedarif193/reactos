/*
 * PROJECT:     ReactOS Raspberry Pi 5 VC4 private UMD/KMD contracts
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     rpi5vc4 vendor-private D3DKMT escape ABI shared by the
 *              miniport, its matching UMD/ICD, and conformance tests.
 *
 * This is not a Windows public ABI.  Win11 parity remains the standard
 * D3DKMT/WDDM2 syscall/DDI surface; this header describes the payload carried
 * through D3DKMT_ESCAPE_DRIVERPRIVATE for this specific VC4 driver stack.
 */

#ifndef _REACTOS_RPI5VC4_UMD_H_
#define _REACTOS_RPI5VC4_UMD_H_

#ifndef _WINDEF_
#include <windef.h>
#endif

#define RPI5VC4_ESCAPE_MAGIC         0x52355645u  /* 'EV5R' */
#define RPI5VC4_ESCAPE_OP_QUERY_INFO 0

#define RPI5VC4_ESCAPE_INFO_ABI_VERSION 2

/*
 * Driver transport/capability bits for the matching VC4 UMD.  These are not
 * Vulkan feature bits; Mesa/v3dv still derives API features from its own V3D
 * compiler and device tables.
 */
#define RPI5VC4_CAP_CL_SUBMIT             (1u << 0)
#define RPI5VC4_CAP_TFU_SUBMIT            (1u << 1)
#define RPI5VC4_CAP_CSD_SUBMIT            (1u << 2)
#define RPI5VC4_CAP_CACHE_FLUSH           (1u << 3)
#define RPI5VC4_CAP_GPUVA_MAP             (1u << 4)
#define RPI5VC4_CAP_ALLOCATION_RELOCATION (1u << 5)
#define RPI5VC4_CAP_MONITORED_FENCE       (1u << 6)
#define RPI5VC4_CAP_SUBMIT_SIGNAL         (1u << 7)
#define RPI5VC4_CAP_CPU_WAIT_SIGNAL       (1u << 8)
#define RPI5VC4_CAP_WIN32_PRESENT         (1u << 9)
#define RPI5VC4_CAP_LINEAR_SCANOUT        (1u << 10)
#define RPI5VC4_CAP_BIN_RENDER_OVERLAP    (1u << 11)

/* GPU nodes shared by the VC4 KMD and its user-mode submission library. */
#define RPI5VC4_NODE_3D                    0u
#define RPI5VC4_NODE_TFU                   1u
#define RPI5VC4_NODE_CSD                   2u
#define RPI5VC4_GPU_NODE_COUNT             3u

/*
 * The low GPU virtual-address space is inherited from the kernel page table
 * and contains the slab, binner overflow, and legacy execution windows.
 * Keep allocations made through the matching UMD above that fixed region.
 */
#define RPI5VC4_DYNAMIC_GPUVA_START 0x20000000ULL

#define RPI5VC4_LINEAR_FORMAT_X8R8G8B8    (1u << 0)
#define RPI5VC4_LINEAR_FORMAT_A8R8G8B8    (1u << 1)

#define RPI5VC4_ALLOCATION_CPU_CACHED     (1u << 0)
#define RPI5VC4_ALLOCATION_VALID_FLAGS    RPI5VC4_ALLOCATION_CPU_CACHED

/*
 * Resource-private data exchanged through the standard WDDM allocation
 * callbacks.  Unlike the allocation payload below, this describes the API
 * resource and survives D3DKMTQueryResourceInfo/D3DKMTOpenResource so that a
 * matching UMD can reconstruct an imported texture without reopening its
 * global share handle.
 *
 * Dimension, Format, Usage, BindFlags, MapFlags and MiscFlags use the public
 * D3D10/11 DDI numeric values.  Keeping those values in the private payload
 * lets the KMD describe standard allocations without depending on Mesa's
 * internal pipe enums.
 */
#define RPI5VC4_RESOURCE_DATA_MAGIC        0x52355244u /* 'DR5R' */
#define RPI5VC4_RESOURCE_DATA_VERSION      2u

#define RPI5VC4_RESOURCE_DIMENSION_TEXTURE2D       3u
#define RPI5VC4_RESOURCE_DXGI_FORMAT_B8G8R8A8_UNORM 87u
#define RPI5VC4_RESOURCE_USAGE_DEFAULT             0u
#define RPI5VC4_RESOURCE_BIND_SHADER_RESOURCE      0x00000008u
#define RPI5VC4_RESOURCE_BIND_RENDER_TARGET        0x00000020u
#define RPI5VC4_RESOURCE_BIND_PRESENT              0x00000080u
#define RPI5VC4_RESOURCE_MISC_SHARED               0x00000002u

#define RPI5VC4_RESOURCE_LAYOUT_LINEAR       1u
#define RPI5VC4_RESOURCE_LAYOUT_V3D_UIF      2u

#define RPI5VC4_RESOURCE_FLAG_PRIMARY         (1u << 0)
#define RPI5VC4_RESOURCE_VALID_FLAGS           RPI5VC4_RESOURCE_FLAG_PRIMARY
#define RPI5VC4_RESOURCE_INVALID_VIDPN_SOURCE  0xffffffffu

typedef struct _RPI5VC4_RESOURCE_DATA
{
    ULONG Magic;
    ULONG Version;
    ULONG Dimension;
    ULONG Format;
    ULONG Usage;
    ULONG BindFlags;
    ULONG MapFlags;
    ULONG MiscFlags;
    ULONG Width;
    ULONG Height;
    ULONG Depth;
    ULONG ArraySize;
    ULONG MipLevels;
    ULONG SampleCount;
    ULONG SampleQuality;
    ULONG AllocationSize;
    ULONG Stride;
    ULONG Layout;
    ULONG Flags;
    ULONG PrimaryVidPnSourceId;
} RPI5VC4_RESOURCE_DATA, *PRPI5VC4_RESOURCE_DATA;

typedef struct _RPI5VC4_ALLOCATION_DATA
{
    ULONG Size;
    ULONG Flags;
} RPI5VC4_ALLOCATION_DATA, *PRPI5VC4_ALLOCATION_DATA;

typedef struct _RPI5VC4_ESCAPE_INFO
{
    ULONG Magic;                     /* in: RPI5VC4_ESCAPE_MAGIC          */
    ULONG Op;                        /* in: RPI5VC4_ESCAPE_OP_*           */
    ULONG V3dReady;                  /* out                               */
    ULONG V3dVersion;                /* out: e.g. 71                      */
    ULONG V3dHubIdent[4];            /* out                               */
    ULONG V3dCoreIdent[3];           /* out                               */
    ULONG SlabGpuVa;                 /* out: GPU VA of VRAM slab base     */
    ULONGLONG SlabPhysical;          /* out                               */
    ULONG SlabSize;                  /* out                               */
    ULONG ScreenWidth;               /* out                               */
    ULONG ScreenHeight;              /* out                               */
    ULONG ScreenPitch;               /* out                               */

    /* ABI v2: pass sizeof(RPI5VC4_ESCAPE_INFO) to receive these fields. */
    ULONG AbiVersion;                /* out                               */
    ULONG Caps;                      /* out: RPI5VC4_CAP_*                */
    ULONG NodeCount;                 /* out                               */
    ULONG MaxPendingSubmits;         /* out                               */
    ULONG AllocationAlignment;       /* out                               */
    ULONG TfuRegisterCount;          /* out                               */
    ULONG CsdConfigCount;            /* out                               */
    ULONG LinearFormatMask;          /* out: RPI5VC4_LINEAR_FORMAT_*      */
    ULONG Reserved[8];               /* out: zero                         */
} RPI5VC4_ESCAPE_INFO, *PRPI5VC4_ESCAPE_INFO;

#endif /* _REACTOS_RPI5VC4_UMD_H_ */
