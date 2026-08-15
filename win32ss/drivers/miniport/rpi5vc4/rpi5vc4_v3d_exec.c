/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bounded V3D 7.1 render-and-readback execution boundary
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * The MMU and submission sequences follow the upstream Linux V3D driver
 * (drivers/gpu/drm/v3d). The fixed V3D 7.1 packet encodings are derived from
 * Mesa's MIT-licensed src/broadcom/cle/v3d_packet.xml and intentionally expose
 * no arbitrary command-list submission to user mode.
 */

#include "rpi5vc4_v3d.h"

#define V3D_HUB_INT_CLR                    0x0058
#define V3D_HUB_INT_MSK_SET                0x0060

#define V3D_MMUC_CONTROL                   0x1000
#define V3D_MMUC_CONTROL_FLUSHING          (1u << 2)
#define V3D_MMUC_CONTROL_FLUSH             (1u << 1)
#define V3D_MMUC_CONTROL_ENABLE            (1u << 0)

#define V3D_MMU_CTL                        0x1200
#define V3D_MMU_CTL_CAP_EXCEEDED           (1u << 27)
#define V3D_MMU_CTL_CAP_EXCEEDED_ABORT     (1u << 26)
#define V3D_MMU_CTL_CAP_EXCEEDED_INT       (1u << 25)
#define V3D_MMU_CTL_PT_INVALID             (1u << 20)
#define V3D_MMU_CTL_PT_INVALID_ABORT       (1u << 19)
#define V3D_MMU_CTL_PT_INVALID_INT         (1u << 18)
#define V3D_MMU_CTL_PT_INVALID_ENABLE      (1u << 16)
#define V3D_MMU_CTL_WRITE_VIOLATION        (1u << 12)
#define V3D_MMU_CTL_WRITE_VIOLATION_ABORT  (1u << 11)
#define V3D_MMU_CTL_WRITE_VIOLATION_INT    (1u << 10)
#define V3D_MMU_CTL_TLB_CLEARING           (1u << 7)
#define V3D_MMU_CTL_TLB_CLEAR              (1u << 2)
#define V3D_MMU_CTL_ENABLE                 (1u << 0)
#define V3D_MMU_PT_PA_BASE                 0x1204
#define V3D_MMU_ILLEGAL_ADDR               0x1230
#define V3D_MMU_ILLEGAL_ADDR_ENABLE        (1u << 31)
#define V3D_MMU_VIO_ADDR                   0x1234

#define V3D_PTE_WRITEABLE                  (1u << 29)
#define V3D_PTE_VALID                      (1u << 28)

#define V3D_CTL_SLCACTL                    0x0024
#define V3D_SLCACTL_INVALIDATE_ALL         0x0F0F0F0Fu
#define V3D_CTL_L2TCACTL                   0x0030
#define V3D_L2TCACTL_L2TFLS                (1u << 0)
#define V3D_CTL_L2TFLSTA                   0x0034
#define V3D_CTL_L2TFLEND                   0x0038
#define V3D_CTL_INT_STS                    0x0050
#define V3D_CTL_INT_CLR                    0x0058
#define V3D_CTL_INT_MSK_SET                0x0060
#define V3D_INT_FRDONE                     (1u << 0)
#define V3D_INT_FLDONE                     (1u << 1)
#define V3D_CLE_CT0EA                      0x0108
#define V3D_CLE_CT1EA                      0x010C
#define V3D_CLE_CT0CA                      0x0110
#define V3D_CLE_CT1CA                      0x0114
#define V3D_CLE_BFC                        0x0134
#define V3D_CLE_RFC                        0x0138
#define V3D_CLE_CT0QTS                     0x015C
#define V3D_CLE_CT0QTS_ENABLE              (1u << 1)
#define V3D_CLE_CT0QBA                     0x0160
#define V3D_CLE_CT1QBA                     0x0164
#define V3D_CLE_CT0QEA                     0x0168
#define V3D_CLE_CT1QEA                     0x016C
#define V3D_CLE_CT0QMA                     0x0170
#define V3D_CLE_CT0QMS                     0x0174
#define V3D_ERR_STAT                       0x0F20
/* VCDI reports that the VCD is idle; unlike the other defined bits, it is not an error. */
#define V3D_ERR_VCDI                       (1u << 12)
#define V3D_ERR_FATAL_MASK                 (0xFFFFu & ~V3D_ERR_VCDI)

#define V3D_SMS_TEE_CS                     0x0400
#define V3D_SMS_STATE_MASK                 0x0000000F
#define V3D_SMS_STATE_IDLE                 0x00000000

#define RPI5VC4_V3D_PAGE_SHIFT             12
#define RPI5VC4_V3D_PAGE_SIZE              (1u << RPI5VC4_V3D_PAGE_SHIFT)
#define RPI5VC4_V3D_PAGE_TABLE_SIZE        (4u * 1024u * 1024u)
#define RPI5VC4_V3D_WORK_SIZE              (64u * 1024u)
#define RPI5VC4_V3D_WORK_GPU_VA            0x01000000u
#define RPI5VC4_V3D_BCL_OFFSET             0x0000u
#define RPI5VC4_V3D_RCL_OFFSET             0x1000u
#define RPI5VC4_V3D_GENERIC_LIST_OFFSET    0x2000u
#define RPI5VC4_V3D_TILE_ALLOC_OFFSET      0x3000u
#define RPI5VC4_V3D_TILE_ALLOC_SIZE        0x3000u
#define RPI5VC4_V3D_TILE_STATE_OFFSET      0x6000u
#define RPI5VC4_V3D_OUTPUT_OFFSET          0x8000u
#define RPI5VC4_V3D_OUTPUT_WIDTH           64u
#define RPI5VC4_V3D_OUTPUT_HEIGHT          64u
#define RPI5VC4_V3D_OUTPUT_STRIDE          \
    (RPI5VC4_V3D_OUTPUT_WIDTH * sizeof(ULONG))
#define RPI5VC4_V3D_OUTPUT_SIZE            \
    (RPI5VC4_V3D_OUTPUT_STRIDE * RPI5VC4_V3D_OUTPUT_HEIGHT)
#define RPI5VC4_V3D_EXPECTED_PIXEL         0xD25AA53Cu
#define RPI5VC4_V3D_SENTINEL_PIXEL         0x6B1D4E97u
#define RPI5VC4_V3D_PTE_PFN_LIMIT          (1u << 24)
#define RPI5VC4_V3D_POLL_COUNT             10000u
#define RPI5VC4_V3D_POLL_US                10u

/* V3D 7.1 control-list opcodes used by Mesa's render-only buffer fill. */
#define V3D71_CL_FLUSH                               4
#define V3D71_CL_START_TILE_BINNING                  6
#define V3D71_CL_END_OF_RENDERING                    13
#define V3D71_CL_RETURN_FROM_SUB_LIST                18
#define V3D71_CL_FLUSH_VCD_CACHE                     19
#define V3D71_CL_START_ADDRESS_OF_GENERIC_TILE_LIST  20
#define V3D71_CL_BRANCH_TO_IMPLICIT_TILE_LIST        21
#define V3D71_CL_SUPERTILE_COORDINATES               23
#define V3D71_CL_CLEAR_RENDER_TARGETS                25
#define V3D71_CL_END_OF_LOADS                        26
#define V3D71_CL_END_OF_TILE_MARKER                  27
#define V3D71_CL_STORE_TILE_BUFFER_GENERAL           29
#define V3D71_CL_NUMBER_OF_LAYERS                    119
#define V3D71_CL_TILE_BINNING_MODE_CFG               120
#define V3D71_CL_TILE_RENDERING_MODE_CFG             121
#define V3D71_CL_MULTICORE_RENDERING_SUPERTILE_CFG   122
#define V3D71_CL_MULTICORE_RENDERING_TILE_LIST_BASE  123
#define V3D71_CL_TILE_COORDINATES                    124
#define V3D71_CL_TILE_COORDINATES_IMPLICIT           125
#define V3D71_CL_TILE_LIST_INITIAL_BLOCK_SIZE        126

#define V3D71_BUFFER_RENDER_TARGET_0 0
#define V3D71_BUFFER_NONE            8
#define V3D71_MEMORY_FORMAT_RASTER   0
#define V3D71_OUTPUT_FORMAT_RGBA8UI  34
#define V3D71_INTERNAL_BPP_32        0
#define V3D71_RT_TYPE_8UI_CLAMPED    20
#define V3D71_TILE_SIZE_LOG2_64      3

#define V3D_PTB_BPOS                 0x030C

typedef struct _RPI5VC4_V3D_CL_BUILDER
{
    PUCHAR Base;
    ULONG Capacity;
    ULONG Length;
    BOOLEAN Overflow;
} RPI5VC4_V3D_CL_BUILDER, *PRPI5VC4_V3D_CL_BUILDER;

static ULONG
Rpi5V3dRead(
    _In_ PVOID Base,
    _In_ ULONG Offset)
{
    return VideoPortReadRegisterUlong((PULONG)((PUCHAR)Base + Offset));
}

static VOID
Rpi5V3dWrite(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    VideoPortWriteRegisterUlong((PULONG)((PUCHAR)Base + Offset), Value);
}

static VOID
Rpi5V3dClByte(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ UCHAR Value)
{
    if (Builder->Length >= Builder->Capacity)
    {
        Builder->Overflow = TRUE;
        return;
    }

    Builder->Base[Builder->Length++] = Value;
}

static VOID
Rpi5V3dClLe16(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG Value)
{
    Rpi5V3dClByte(Builder, (UCHAR)Value);
    Rpi5V3dClByte(Builder, (UCHAR)(Value >> 8));
}

static VOID
Rpi5V3dClLe32(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG Value)
{
    Rpi5V3dClByte(Builder, (UCHAR)Value);
    Rpi5V3dClByte(Builder, (UCHAR)(Value >> 8));
    Rpi5V3dClByte(Builder, (UCHAR)(Value >> 16));
    Rpi5V3dClByte(Builder, (UCHAR)(Value >> 24));
}

static VOID
Rpi5V3dClRenderingCommon(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder)
{
    ULONG Log2Width = V3D71_TILE_SIZE_LOG2_64;
    ULONG Log2Height = V3D71_TILE_SIZE_LOG2_64;

    Rpi5V3dClByte(Builder, V3D71_CL_TILE_RENDERING_MODE_CFG);
    Rpi5V3dClByte(Builder, 0); /* sub-id 0, one render target minus one */
    Rpi5V3dClLe16(Builder, RPI5VC4_V3D_OUTPUT_WIDTH);
    Rpi5V3dClLe16(Builder, RPI5VC4_V3D_OUTPUT_HEIGHT);
    Rpi5V3dClByte(Builder, (1u << 6)); /* early-z disable */
    Rpi5V3dClByte(Builder,
                  (UCHAR)((Log2Width << 4) | (Log2Height << 7)));
    Rpi5V3dClByte(Builder, (UCHAR)(Log2Height >> 1));
}

static VOID
Rpi5V3dClRenderTargetPart1(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG ClearColor)
{
    ULONG Stride = 32; /* (64 pixels * one 32-bit word) / two rows */
    ULONG PackedStride = (Stride - 1) << 2;

    Rpi5V3dClByte(Builder, V3D71_CL_TILE_RENDERING_MODE_CFG);
    Rpi5V3dClByte(Builder, 2); /* sub-id 2, RT 0, tile-buffer base 0 */
    Rpi5V3dClByte(Builder, 0);
    Rpi5V3dClByte(Builder, (UCHAR)PackedStride);
    Rpi5V3dClByte(Builder,
                  (UCHAR)((PackedStride >> 8) |
                          (V3D71_INTERNAL_BPP_32 << 1) |
                          (V3D71_RT_TYPE_8UI_CLAMPED << 3)));
    Rpi5V3dClLe32(Builder, ClearColor);
}

static VOID
Rpi5V3dClZsClear(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder)
{
    Rpi5V3dClByte(Builder, V3D71_CL_TILE_RENDERING_MODE_CFG);
    Rpi5V3dClByte(Builder, 1); /* sub-id 1 */
    Rpi5V3dClByte(Builder, 0); /* stencil */
    Rpi5V3dClLe32(Builder, 0x3F800000u); /* depth 1.0f */
    Rpi5V3dClLe16(Builder, 0);
}

static VOID
Rpi5V3dClStoreGeneral(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG Buffer,
    _In_ ULONG Format,
    _In_ ULONG Stride,
    _In_ ULONG Address)
{
    ULONG PackedStride = Stride << 4;

    Rpi5V3dClByte(Builder, V3D71_CL_STORE_TILE_BUFFER_GENERAL);
    Rpi5V3dClByte(Builder,
                  (UCHAR)((V3D71_MEMORY_FORMAT_RASTER << 4) | Buffer));
    Rpi5V3dClByte(Builder, (UCHAR)(Format << 4));
    Rpi5V3dClByte(Builder, (UCHAR)(Format >> 4));
    Rpi5V3dClByte(Builder, (UCHAR)PackedStride);
    Rpi5V3dClByte(Builder, (UCHAR)(PackedStride >> 8));
    Rpi5V3dClByte(Builder, (UCHAR)(PackedStride >> 16));
    Rpi5V3dClLe16(Builder, 0); /* raster stores do not use height */
    Rpi5V3dClLe32(Builder, Address);
}

static VOID
Rpi5V3dClTileCoordinates(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder)
{
    Rpi5V3dClByte(Builder, V3D71_CL_TILE_COORDINATES);
    Rpi5V3dClByte(Builder, 0);
    Rpi5V3dClByte(Builder, 0);
    Rpi5V3dClByte(Builder, 0);
}

static VOID
Rpi5V3dClDummyTile(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ BOOLEAN Clear)
{
    Rpi5V3dClTileCoordinates(Builder);
    Rpi5V3dClByte(Builder, V3D71_CL_END_OF_LOADS);
    Rpi5V3dClStoreGeneral(Builder, V3D71_BUFFER_NONE, 0, 0, 0);
    if (Clear)
        Rpi5V3dClByte(Builder, V3D71_CL_CLEAR_RENDER_TARGETS);
    Rpi5V3dClByte(Builder, V3D71_CL_END_OF_TILE_MARKER);
}

static VOID
Rpi5V3dClSupertileConfig(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder)
{
    Rpi5V3dClByte(Builder, V3D71_CL_MULTICORE_RENDERING_SUPERTILE_CFG);
    Rpi5V3dClByte(Builder, 0); /* supertile width: one tile minus one */
    Rpi5V3dClByte(Builder, 0); /* supertile height: one tile minus one */
    Rpi5V3dClByte(Builder, 1); /* frame width in supertiles */
    Rpi5V3dClByte(Builder, 1); /* frame height in supertiles */
    Rpi5V3dClByte(Builder, 1); /* frame width in tiles, low bits */
    Rpi5V3dClByte(Builder, 0x10); /* frame height in tiles */
    Rpi5V3dClByte(Builder, 0);
    Rpi5V3dClByte(Builder, 0); /* one tile list minus one */
}

static VOID
Rpi5V3dBuildBinningList(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Bcl)
{
    /* One layer, followed by Mesa's V3D 7.1 one-tile binning prologue. */
    Rpi5V3dClByte(Bcl, V3D71_CL_NUMBER_OF_LAYERS);
    Rpi5V3dClByte(Bcl, 0); /* one layer minus one */

    Rpi5V3dClByte(Bcl, V3D71_CL_TILE_BINNING_MODE_CFG);
    Rpi5V3dClByte(Bcl, (1u << 2)); /* 128-byte initial, 64-byte overflow */
    Rpi5V3dClByte(Bcl,
                  V3D71_TILE_SIZE_LOG2_64 |
                  (V3D71_TILE_SIZE_LOG2_64 << 3));
    Rpi5V3dClLe16(Bcl, 0);
    Rpi5V3dClLe16(Bcl, RPI5VC4_V3D_OUTPUT_WIDTH - 1);
    Rpi5V3dClLe16(Bcl, RPI5VC4_V3D_OUTPUT_HEIGHT - 1);
    Rpi5V3dClByte(Bcl, V3D71_CL_FLUSH_VCD_CACHE);
    Rpi5V3dClByte(Bcl, V3D71_CL_START_TILE_BINNING);
    Rpi5V3dClByte(Bcl, V3D71_CL_FLUSH);
}

static BOOLEAN
Rpi5V3dBuildControlLists(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG BclBytes,
    _Out_ PULONG RclBytes,
    _Out_ PULONG GenericBytes)
{
    RPI5VC4_V3D_CL_BUILDER Bcl;
    RPI5VC4_V3D_CL_BUILDER Rcl;
    RPI5VC4_V3D_CL_BUILDER Generic;
    ULONG WorkGpuVa = RPI5VC4_V3D_WORK_GPU_VA;
    ULONG GenericStart = WorkGpuVa + RPI5VC4_V3D_GENERIC_LIST_OFFSET;
    ULONG GenericEnd;

    Bcl.Base = (PUCHAR)DeviceExtension->V3dWorkVa +
               RPI5VC4_V3D_BCL_OFFSET;
    Bcl.Capacity = RPI5VC4_V3D_PAGE_SIZE;
    Bcl.Length = 0;
    Bcl.Overflow = FALSE;

    Rcl.Base = (PUCHAR)DeviceExtension->V3dWorkVa +
               RPI5VC4_V3D_RCL_OFFSET;
    Rcl.Capacity = RPI5VC4_V3D_PAGE_SIZE;
    Rcl.Length = 0;
    Rcl.Overflow = FALSE;

    Generic.Base = (PUCHAR)DeviceExtension->V3dWorkVa +
                   RPI5VC4_V3D_GENERIC_LIST_OFFSET;
    Generic.Capacity = RPI5VC4_V3D_PAGE_SIZE;
    Generic.Length = 0;
    Generic.Overflow = FALSE;

    Rpi5V3dBuildBinningList(&Bcl);

    /* Generic per-tile list used by the one 64x64 supertile. */
    Rpi5V3dClByte(&Generic, V3D71_CL_TILE_COORDINATES_IMPLICIT);
    Rpi5V3dClByte(&Generic, V3D71_CL_END_OF_LOADS);
    Rpi5V3dClByte(&Generic, V3D71_CL_BRANCH_TO_IMPLICIT_TILE_LIST);
    Rpi5V3dClByte(&Generic, 0); /* tile-list set 0 */
    Rpi5V3dClStoreGeneral(&Generic,
                          V3D71_BUFFER_RENDER_TARGET_0,
                          V3D71_OUTPUT_FORMAT_RGBA8UI,
                          RPI5VC4_V3D_OUTPUT_STRIDE,
                          WorkGpuVa + RPI5VC4_V3D_OUTPUT_OFFSET);
    Rpi5V3dClByte(&Generic, V3D71_CL_END_OF_TILE_MARKER);
    Rpi5V3dClByte(&Generic, V3D71_CL_RETURN_FROM_SUB_LIST);
    GenericEnd = GenericStart + Generic.Length;

    /* Mesa's V3D 7.1 render-only buffer-fill RCL, including GFXH-1742. */
    Rpi5V3dClRenderingCommon(&Rcl);
    Rpi5V3dClRenderTargetPart1(&Rcl, RPI5VC4_V3D_EXPECTED_PIXEL);
    Rpi5V3dClZsClear(&Rcl);
    Rpi5V3dClByte(&Rcl, V3D71_CL_TILE_LIST_INITIAL_BLOCK_SIZE);
    Rpi5V3dClByte(&Rcl, 5); /* 128-byte first block, auto-chain enabled */
    Rpi5V3dClByte(&Rcl, V3D71_CL_MULTICORE_RENDERING_TILE_LIST_BASE);
    Rpi5V3dClLe32(&Rcl,
                  WorkGpuVa + RPI5VC4_V3D_TILE_ALLOC_OFFSET);
    Rpi5V3dClSupertileConfig(&Rcl);
    Rpi5V3dClDummyTile(&Rcl, TRUE);
    Rpi5V3dClDummyTile(&Rcl, FALSE);
    Rpi5V3dClByte(&Rcl, V3D71_CL_FLUSH_VCD_CACHE);
    Rpi5V3dClByte(&Rcl, V3D71_CL_START_ADDRESS_OF_GENERIC_TILE_LIST);
    Rpi5V3dClLe32(&Rcl, GenericStart);
    Rpi5V3dClLe32(&Rcl, GenericEnd);
    Rpi5V3dClByte(&Rcl, V3D71_CL_SUPERTILE_COORDINATES);
    Rpi5V3dClByte(&Rcl, 0);
    Rpi5V3dClByte(&Rcl, 0);
    Rpi5V3dClByte(&Rcl, V3D71_CL_END_OF_RENDERING);

    *BclBytes = Bcl.Length;
    *RclBytes = Rcl.Length;
    *GenericBytes = Generic.Length;
    return !Bcl.Overflow && !Rcl.Overflow && !Generic.Overflow;
}

static VOID
Rpi5V3dReleaseExecutionBuffers(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->V3dWorkVa != NULL)
    {
        VideoPortReleaseCommonBuffer(DeviceExtension,
                                     DeviceExtension->V3dDmaAdapter,
                                     RPI5VC4_V3D_WORK_SIZE,
                                     DeviceExtension->V3dWorkLogical,
                                     DeviceExtension->V3dWorkVa,
                                     FALSE);
        DeviceExtension->V3dWorkVa = NULL;
        DeviceExtension->V3dWorkLogical.QuadPart = 0;
    }

    if (DeviceExtension->V3dPageTableVa != NULL)
    {
        VideoPortReleaseCommonBuffer(DeviceExtension,
                                     DeviceExtension->V3dDmaAdapter,
                                     RPI5VC4_V3D_PAGE_TABLE_SIZE,
                                     DeviceExtension->V3dPageTableLogical,
                                     DeviceExtension->V3dPageTableVa,
                                     FALSE);
        DeviceExtension->V3dPageTableVa = NULL;
        DeviceExtension->V3dPageTableLogical.QuadPart = 0;
    }

    if (DeviceExtension->V3dDmaAdapter != NULL)
    {
        VideoPortPutDmaAdapter(DeviceExtension,
                               DeviceExtension->V3dDmaAdapter);
        DeviceExtension->V3dDmaAdapter = NULL;
    }
}

static BOOLEAN
Rpi5V3dAllocateExecutionBuffers(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    VP_DEVICE_DESCRIPTION Description;

    if (DeviceExtension->V3dPageTableVa != NULL &&
        DeviceExtension->V3dWorkVa != NULL)
    {
        return TRUE;
    }

    VideoPortZeroMemory(&Description, sizeof(Description));
    Description.ScatterGather = FALSE;
    Description.Dma32BitAddresses = TRUE;
    Description.Dma64BitAddresses = FALSE;
    Description.MaximumLength = RPI5VC4_V3D_PAGE_TABLE_SIZE;

    /* VideoPort resolves this adapter through this instance's ACPI GPU0 PDO. */
    DeviceExtension->V3dDmaAdapter =
        VideoPortGetDmaAdapter(DeviceExtension, &Description);
    if (DeviceExtension->V3dDmaAdapter == NULL)
        return FALSE;

    DeviceExtension->V3dPageTableVa =
        VideoPortAllocateCommonBuffer(DeviceExtension,
                                      DeviceExtension->V3dDmaAdapter,
                                      RPI5VC4_V3D_PAGE_TABLE_SIZE,
                                      &DeviceExtension->V3dPageTableLogical,
                                      FALSE,
                                      NULL);
    if (DeviceExtension->V3dPageTableVa == NULL)
    {
        Rpi5V3dReleaseExecutionBuffers(DeviceExtension);
        return FALSE;
    }

    DeviceExtension->V3dWorkVa =
        VideoPortAllocateCommonBuffer(DeviceExtension,
                                      DeviceExtension->V3dDmaAdapter,
                                      RPI5VC4_V3D_WORK_SIZE,
                                      &DeviceExtension->V3dWorkLogical,
                                      FALSE,
                                      NULL);
    if (DeviceExtension->V3dWorkVa == NULL)
    {
        Rpi5V3dReleaseExecutionBuffers(DeviceExtension);
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
Rpi5V3dBuildPageTable(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PULONG PageTable = DeviceExtension->V3dPageTableVa;
    ULONGLONG WorkPfn =
        (ULONGLONG)DeviceExtension->V3dWorkLogical.QuadPart >>
        RPI5VC4_V3D_PAGE_SHIFT;
    ULONGLONG PageTablePfn =
        (ULONGLONG)DeviceExtension->V3dPageTableLogical.QuadPart >>
        RPI5VC4_V3D_PAGE_SHIFT;
    ULONG FirstPte =
        RPI5VC4_V3D_WORK_GPU_VA >> RPI5VC4_V3D_PAGE_SHIFT;
    ULONG Pages = RPI5VC4_V3D_WORK_SIZE >> RPI5VC4_V3D_PAGE_SHIFT;
    ULONG Index;

    if ((DeviceExtension->V3dWorkLogical.QuadPart &
         (RPI5VC4_V3D_PAGE_SIZE - 1)) != 0 ||
        (DeviceExtension->V3dPageTableLogical.QuadPart &
         (RPI5VC4_V3D_PAGE_SIZE - 1)) != 0 ||
        WorkPfn + Pages > RPI5VC4_V3D_PTE_PFN_LIMIT ||
        PageTablePfn > MAXULONG)
    {
        return FALSE;
    }

    VideoPortZeroMemory(PageTable, RPI5VC4_V3D_PAGE_TABLE_SIZE);
    for (Index = 0; Index < Pages; Index++)
    {
        PageTable[FirstPte + Index] =
            (ULONG)(WorkPfn + Index) | V3D_PTE_WRITEABLE | V3D_PTE_VALID;
    }

    KeMemoryBarrier();
    return TRUE;
}

static BOOLEAN
Rpi5V3dWaitForClear(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Mask)
{
    ULONG Poll;

    for (Poll = 0; Poll < RPI5VC4_V3D_POLL_COUNT; Poll++)
    {
        if ((Rpi5V3dRead(Base, Offset) & Mask) == 0)
            return TRUE;
        VideoPortStallExecution(RPI5VC4_V3D_POLL_US);
    }

    return FALSE;
}

static BOOLEAN
Rpi5V3dProgramMmu(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    ULONG MmuControl;

    Rpi5V3dWrite(DeviceExtension->V3dHubBase,
                 V3D_MMU_PT_PA_BASE,
                 (ULONG)(DeviceExtension->V3dPageTableLogical.QuadPart >>
                         RPI5VC4_V3D_PAGE_SHIFT));
    MmuControl = V3D_MMU_CTL_ENABLE |
                 V3D_MMU_CTL_PT_INVALID_ENABLE |
                 V3D_MMU_CTL_PT_INVALID_ABORT |
                 V3D_MMU_CTL_PT_INVALID_INT |
                 V3D_MMU_CTL_WRITE_VIOLATION_ABORT |
                 V3D_MMU_CTL_WRITE_VIOLATION_INT |
                 V3D_MMU_CTL_CAP_EXCEEDED_ABORT |
                 V3D_MMU_CTL_CAP_EXCEEDED_INT;
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_MMU_CTL, MmuControl);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase,
                 V3D_MMU_ILLEGAL_ADDR,
                 (ULONG)(DeviceExtension->V3dWorkLogical.QuadPart >>
                         RPI5VC4_V3D_PAGE_SHIFT) |
                 V3D_MMU_ILLEGAL_ADDR_ENABLE);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase,
                 V3D_MMUC_CONTROL,
                 V3D_MMUC_CONTROL_ENABLE | V3D_MMUC_CONTROL_FLUSH);
    if (!Rpi5V3dWaitForClear(DeviceExtension->V3dHubBase,
                             V3D_MMUC_CONTROL,
                             V3D_MMUC_CONTROL_FLUSHING))
    {
        return FALSE;
    }

    Rpi5V3dWrite(DeviceExtension->V3dHubBase,
                 V3D_MMU_CTL,
                 Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_MMU_CTL) |
                 V3D_MMU_CTL_TLB_CLEAR);
    return Rpi5V3dWaitForClear(DeviceExtension->V3dHubBase,
                               V3D_MMU_CTL,
                               V3D_MMU_CTL_TLB_CLEARING);
}

static BOOLEAN
Rpi5V3dEnginesIdle(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Ct0Current =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_CT0CA);
    ULONG Ct0End =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_CT0EA);
    ULONG Ct1Current =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_CT1CA);
    ULONG Ct1End =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_CT1EA);

    return Ct0Current == Ct0End && Ct1Current == Ct1End;
}

VP_STATUS
Rpi5V3dRunSelfTest(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PRPI5VC4_V3D_SELFTEST Result)
{
    PULONG Output;
    ULONG BclBytes = 0;
    ULONG RclBytes = 0;
    ULONG GenericBytes = 0;
    ULONG BclStart =
        RPI5VC4_V3D_WORK_GPU_VA + RPI5VC4_V3D_BCL_OFFSET;
    ULONG RclStart =
        RPI5VC4_V3D_WORK_GPU_VA + RPI5VC4_V3D_RCL_OFFSET;
    ULONG Poll;
    ULONG MmuFaultMask = V3D_MMU_CTL_CAP_EXCEEDED |
                         V3D_MMU_CTL_PT_INVALID |
                         V3D_MMU_CTL_WRITE_VIOLATION;
    BOOLEAN BinningCompleted = FALSE;
    BOOLEAN Completed = FALSE;

    VideoPortZeroMemory(Result, sizeof(*Result));
    Result->Size = sizeof(*Result);
    Result->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_NOT_SUPPORTED;
    Result->ExpectedPixel = RPI5VC4_V3D_EXPECTED_PIXEL;

    if (InterlockedCompareExchange(&DeviceExtension->V3dSelfTestBusy,
                                   1,
                                   0) != 0)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_BUSY;
        return NO_ERROR;
    }

    if ((DeviceExtension->V3dFlags & RPI5VC4_V3D_FLAG_IDENT_VALID) == 0 ||
        DeviceExtension->V3dVersion != 71 ||
        DeviceExtension->V3dHubBase == NULL ||
        DeviceExtension->V3dCoreBase == NULL ||
        DeviceExtension->V3dSmsBase == NULL ||
        (Rpi5V3dRead(DeviceExtension->V3dSmsBase, V3D_SMS_TEE_CS) &
         V3D_SMS_STATE_MASK) != V3D_SMS_STATE_IDLE)
    {
        goto Done;
    }

    if (DeviceExtension->V3dExecutionPoisoned)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_POISONED;
        goto Done;
    }

    if (!Rpi5V3dAllocateExecutionBuffers(DeviceExtension))
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_ALLOCATION_FAILED;
        goto Done;
    }
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_DMA_ALLOCATED;

    if (!Rpi5V3dBuildPageTable(DeviceExtension))
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_ADDRESS_UNSUPPORTED;
        Rpi5V3dReleaseExecutionBuffers(DeviceExtension);
        Result->Flags &= ~RPI5VC4_V3D_SELFTEST_FLAG_DMA_ALLOCATED;
        goto Done;
    }

    VideoPortZeroMemory(DeviceExtension->V3dWorkVa,
                        RPI5VC4_V3D_WORK_SIZE);
    Output = (PULONG)((PUCHAR)DeviceExtension->V3dWorkVa +
                      RPI5VC4_V3D_OUTPUT_OFFSET);
    for (Poll = 0;
         Poll < RPI5VC4_V3D_OUTPUT_SIZE / sizeof(ULONG);
         Poll++)
    {
        Output[Poll] = RPI5VC4_V3D_SENTINEL_PIXEL;
    }

    if (!Rpi5V3dBuildControlLists(DeviceExtension,
                                  &BclBytes,
                                  &RclBytes,
                                  &GenericBytes))
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_RENDER_ERROR;
        goto Done;
    }
    Result->BinningControlListBytes = BclBytes;
    Result->RenderControlListBytes = RclBytes;
    Result->GenericTileListBytes = GenericBytes;

    if (!Rpi5V3dEnginesIdle(DeviceExtension))
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_ENGINE_BUSY;
        goto Capture;
    }

    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_INT_MSK_SET, ~0u);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_INT_CLR, ~0u);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_HUB_INT_MSK_SET, ~0u);
    Rpi5V3dWrite(DeviceExtension->V3dHubBase, V3D_HUB_INT_CLR, ~0u);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_L2TFLSTA, 0);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CTL_L2TFLEND, ~0u);

    if (!Rpi5V3dProgramMmu(DeviceExtension))
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_MMU_TIMEOUT;
        DeviceExtension->V3dExecutionPoisoned = TRUE;
        goto Capture;
    }
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_MMU_PROGRAMMED;

    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CTL_INT_CLR,
                 V3D_INT_FRDONE | V3D_INT_FLDONE);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CTL_L2TCACTL,
                 V3D_L2TCACTL_L2TFLS);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CTL_SLCACTL,
                 V3D_SLCACTL_INVALIDATE_ALL);

    Result->BfcBefore =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_BFC) & 0xFF;
    KeMemoryBarrier();
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_PTB_BPOS, 0);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CLE_CT0QMA,
                 RPI5VC4_V3D_WORK_GPU_VA +
                 RPI5VC4_V3D_TILE_ALLOC_OFFSET);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CLE_CT0QMS,
                 RPI5VC4_V3D_TILE_ALLOC_SIZE);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CLE_CT0QTS,
                 (RPI5VC4_V3D_WORK_GPU_VA +
                  RPI5VC4_V3D_TILE_STATE_OFFSET) |
                 V3D_CLE_CT0QTS_ENABLE);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CLE_CT0QBA, BclStart);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CLE_CT0QEA,
                 BclStart + BclBytes);
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_BINNING_KICKED;

    for (Poll = 0; Poll < RPI5VC4_V3D_POLL_COUNT; Poll++)
    {
        Result->CoreInterruptStatus =
            Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_INT_STS);
        Result->BfcAfter =
            Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_BFC) & 0xFF;
        Result->MmuControl =
            Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_MMU_CTL);
        Result->ErrorStatus =
            Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_ERR_STAT);

        if ((Result->MmuControl & MmuFaultMask) != 0 ||
            (Result->ErrorStatus & V3D_ERR_FATAL_MASK) != 0)
        {
            break;
        }

        if ((Result->CoreInterruptStatus & V3D_INT_FLDONE) != 0 ||
            Result->BfcAfter != Result->BfcBefore)
        {
            BinningCompleted = TRUE;
            break;
        }

        VideoPortStallExecution(RPI5VC4_V3D_POLL_US);
    }
    Result->BinningPollCount =
        Poll + (Poll < RPI5VC4_V3D_POLL_COUNT ? 1 : 0);

    if ((Result->MmuControl & MmuFaultMask) != 0)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_MMU_FAULT;
        DeviceExtension->V3dExecutionPoisoned = TRUE;
        goto Capture;
    }

    if ((Result->ErrorStatus & V3D_ERR_FATAL_MASK) != 0)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_BINNING_ERROR;
        DeviceExtension->V3dExecutionPoisoned = TRUE;
        goto Capture;
    }

    if (!BinningCompleted)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_BINNING_TIMEOUT;
        DeviceExtension->V3dExecutionPoisoned = TRUE;
        goto Capture;
    }
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_BINNING_DONE;

    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CTL_INT_CLR,
                 V3D_INT_FLDONE);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CTL_L2TCACTL,
                 V3D_L2TCACTL_L2TFLS);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CTL_SLCACTL,
                 V3D_SLCACTL_INVALIDATE_ALL);

    Result->RfcBefore =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_RFC) & 0xFF;
    KeMemoryBarrier();
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase, V3D_CLE_CT1QBA, RclStart);
    Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                 V3D_CLE_CT1QEA,
                 RclStart + RclBytes);
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_JOB_KICKED;

    for (Poll = 0; Poll < RPI5VC4_V3D_POLL_COUNT; Poll++)
    {
        Result->CoreInterruptStatus =
            Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_INT_STS);
        Result->RfcAfter =
            Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_RFC) & 0xFF;
        Result->MmuControl =
            Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_MMU_CTL);
        Result->ErrorStatus =
            Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_ERR_STAT);

        if ((Result->MmuControl & MmuFaultMask) != 0 ||
            (Result->ErrorStatus & V3D_ERR_FATAL_MASK) != 0)
        {
            break;
        }

        if ((Result->CoreInterruptStatus & V3D_INT_FRDONE) != 0 ||
            Result->RfcAfter != Result->RfcBefore)
        {
            Completed = TRUE;
            break;
        }

        VideoPortStallExecution(RPI5VC4_V3D_POLL_US);
    }
    Result->PollCount = Poll + (Poll < RPI5VC4_V3D_POLL_COUNT ? 1 : 0);

    if ((Result->MmuControl & MmuFaultMask) != 0)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_MMU_FAULT;
        DeviceExtension->V3dExecutionPoisoned = TRUE;
        goto Capture;
    }

    if ((Result->ErrorStatus & V3D_ERR_FATAL_MASK) != 0)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_RENDER_ERROR;
        DeviceExtension->V3dExecutionPoisoned = TRUE;
        goto Capture;
    }

    if (!Completed)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_RENDER_TIMEOUT;
        DeviceExtension->V3dExecutionPoisoned = TRUE;
        goto Capture;
    }
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_JOB_COMPLETED;

    KeMemoryBarrier();
    Result->FirstPixel = Output[0];
    Result->CenterPixel =
        Output[(RPI5VC4_V3D_OUTPUT_HEIGHT / 2) *
               RPI5VC4_V3D_OUTPUT_WIDTH +
               (RPI5VC4_V3D_OUTPUT_WIDTH / 2)];
    Result->LastPixel =
        Output[RPI5VC4_V3D_OUTPUT_WIDTH *
               RPI5VC4_V3D_OUTPUT_HEIGHT - 1];
    for (Poll = 0;
         Poll < RPI5VC4_V3D_OUTPUT_SIZE / sizeof(ULONG);
         Poll++)
    {
        if (Output[Poll] != RPI5VC4_V3D_EXPECTED_PIXEL)
            Result->MismatchCount++;
    }
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_READBACK_VALID;

    if (Result->MismatchCount != 0)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_READBACK_MISMATCH;
        goto Capture;
    }

    Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS;
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_PASSED;

Capture:
    Result->CoreInterruptStatus =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CTL_INT_STS);
    Result->BfcAfter =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_BFC) & 0xFF;
    Result->RfcAfter =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_RFC) & 0xFF;
    Result->Ct0Current =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_CT0CA);
    Result->Ct0End =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_CT0EA);
    Result->Ct1Current =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_CT1CA);
    Result->Ct1End =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_CLE_CT1EA);
    Result->MmuControl =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_MMU_CTL);
    Result->MmuViolationAddress =
        Rpi5V3dRead(DeviceExtension->V3dHubBase, V3D_MMU_VIO_ADDR);
    Result->ErrorStatus =
        Rpi5V3dRead(DeviceExtension->V3dCoreBase, V3D_ERR_STAT);
    if ((Result->CoreInterruptStatus &
         (V3D_INT_FRDONE | V3D_INT_FLDONE)) != 0)
    {
        Rpi5V3dWrite(DeviceExtension->V3dCoreBase,
                     V3D_CTL_INT_CLR,
                     Result->CoreInterruptStatus &
                     (V3D_INT_FRDONE | V3D_INT_FLDONE));
    }

    DbgPrint("RPI5VC4: V3D selftest status=%lu flags=%08lx "
             "bfc=%lu/%lu ct0=%08lx/%08lx rfc=%lu/%lu "
             "ct1=%08lx/%08lx mmu=%08lx/%08lx err=%08lx "
             "pixels=%08lx/%08lx/%08lx mismatches=%lu\n",
             Result->Status,
             Result->Flags,
             Result->BfcBefore,
             Result->BfcAfter,
             Result->Ct0Current,
             Result->Ct0End,
             Result->RfcBefore,
             Result->RfcAfter,
             Result->Ct1Current,
             Result->Ct1End,
             Result->MmuControl,
             Result->MmuViolationAddress,
             Result->ErrorStatus,
             Result->FirstPixel,
             Result->CenterPixel,
             Result->LastPixel,
             Result->MismatchCount);

Done:
    InterlockedExchange(&DeviceExtension->V3dSelfTestBusy, 0);
    return NO_ERROR;
}
