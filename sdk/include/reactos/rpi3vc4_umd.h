/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 private UMD/KMD contracts
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Vendor-private D3DKMT escape ABI shared by the VC4 miniport,
 *              its matching user-mode driver, and focused hardware tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _REACTOS_RPI3VC4_UMD_H_
#define _REACTOS_RPI3VC4_UMD_H_

#ifndef _WINDEF_
#include <windef.h>
#endif

#define RPI3VC4_ESCAPE_MAGIC                 0x52335645u /* 'EV3R' */
#define RPI3VC4_ESCAPE_OP_QUERY_INFO         0u
#define RPI3VC4_ESCAPE_INFO_ABI_VERSION      3u

#define RPI3VC4_CAP_POWER_CONTROL            (1u << 0)
#define RPI3VC4_CAP_IDENT_VALID              (1u << 1)
#define RPI3VC4_CAP_LINEAR_SCANOUT           (1u << 2)
#define RPI3VC4_CAP_RENDER_THREAD            (1u << 3)
#define RPI3VC4_CAP_VALIDATED_CL_SUBMIT       (1u << 4)
#define RPI3VC4_CAP_MONITORED_FENCE           (1u << 5)
#define RPI3VC4_CAP_OPENGL_20                 (1u << 6)

/*
 * The VC4 has no GPU MMU.  User mode therefore submits untrusted binning,
 * shader-record, and uniform byte streams rather than executable GPU
 * addresses.  Dxgkrnl pins the BO list named by the transport resource list;
 * the miniport validates and relocates the streams into the DMA buffer before
 * the scheduler can start the hardware.
 */
#define RPI3VC4_DMA_PACKET_MAGIC              0x52335644u /* 'DV3R' */
#define RPI3VC4_DMA_OP_VALIDATED_CL           1u
#define RPI3VC4_SUBMIT_CL_VERSION             1u
#define RPI3VC4_MAX_SUBMIT_RESOURCES          4096u
#define RPI3VC4_MAX_SUBMIT_STREAM_BYTES       (16u * 1024u * 1024u)
#define RPI3VC4_SUBMIT_SCRATCH_MINIMUM        (2u * 1024u * 1024u)
#define RPI3VC4_RESOURCE_SHADER                (1u << 0)

typedef struct _RPI3VC4_SUBMIT_RCL_SURFACE
{
    ULONG hindex;
    ULONG offset;
    USHORT bits;
    USHORT flags;
} RPI3VC4_SUBMIT_RCL_SURFACE, *PRPI3VC4_SUBMIT_RCL_SURFACE;

#define RPI3VC4_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES (1u << 0)
#define RPI3VC4_SUBMIT_CL_USE_CLEAR_COLOR            (1u << 0)
#define RPI3VC4_SUBMIT_CL_FIXED_RCL_ORDER             (1u << 1)
#define RPI3VC4_SUBMIT_CL_RCL_ORDER_INCREASING_X      (1u << 2)
#define RPI3VC4_SUBMIT_CL_RCL_ORDER_INCREASING_Y      (1u << 3)

typedef struct _RPI3VC4_SUBMIT_CL
{
    ULONG Version;
    ULONG Size;
    ULONG BinClOffset;
    ULONG ShaderRecOffset;
    ULONG UniformsOffset;
    ULONG BinClSize;
    ULONG ShaderRecSize;
    ULONG ShaderRecCount;
    ULONG UniformsSize;
    ULONG BoHandleCount;
    USHORT Width;
    USHORT Height;
    UCHAR MinXTile;
    UCHAR MinYTile;
    UCHAR MaxXTile;
    UCHAR MaxYTile;
    RPI3VC4_SUBMIT_RCL_SURFACE ColorRead;
    RPI3VC4_SUBMIT_RCL_SURFACE ColorWrite;
    RPI3VC4_SUBMIT_RCL_SURFACE ZsRead;
    RPI3VC4_SUBMIT_RCL_SURFACE ZsWrite;
    RPI3VC4_SUBMIT_RCL_SURFACE MsaaColorWrite;
    RPI3VC4_SUBMIT_RCL_SURFACE MsaaZsWrite;
    ULONG ClearColor[2];
    ULONG ClearZ;
    UCHAR ClearS;
    UCHAR Reserved0[3];
    ULONG Flags;
    ULONG ScratchBytes;
    ULONG ResourceFlagsOffset;
    ULONG Reserved[5];
} RPI3VC4_SUBMIT_CL, *PRPI3VC4_SUBMIT_CL;

/* Kernel-generated DMA record.  No field is accepted directly from user
 * mode; Render overwrites the output buffer with this record after successful
 * validation and relocation. */
typedef struct _RPI3VC4_DMA_PACKET
{
    ULONG Magic;
    ULONG Op;
    ULONG Size;
    ULONG Flags;
    ULONG Ct0Start;
    ULONG Ct0End;
    ULONG Ct1Start;
    ULONG Ct1End;
    ULONG TileAllocationAddress;
    ULONG TileAllocationSize;
    ULONG TileStateAddress;
    ULONG BinnerOverflowAddress;
    ULONG BinnerOverflowSize;
    ULONG Reserved[3];
} RPI3VC4_DMA_PACKET, *PRPI3VC4_DMA_PACKET;

typedef struct _RPI3VC4_ESCAPE_INFO
{
    ULONG Magic;
    ULONG Op;
    ULONG Size;
    ULONG AbiVersion;
    LONG InitializationStatus;
    ULONG Caps;
    ULONG V3dReady;
    ULONG V3dIdent0;
    ULONG V3dIdent1;
    ULONG V3dIdent2;
    ULONGLONG V3dPhysical;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    ULONG ScreenPitch;
    LONG RenderTestStatus;
    ULONG RenderTestPixel;
    ULONG Reserved[6];
} RPI3VC4_ESCAPE_INFO, *PRPI3VC4_ESCAPE_INFO;

#endif /* _REACTOS_RPI3VC4_UMD_H_ */
