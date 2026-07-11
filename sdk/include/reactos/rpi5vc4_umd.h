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

#define RPI5VC4_LINEAR_FORMAT_X8R8G8B8    (1u << 0)
#define RPI5VC4_LINEAR_FORMAT_A8R8G8B8    (1u << 1)

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
