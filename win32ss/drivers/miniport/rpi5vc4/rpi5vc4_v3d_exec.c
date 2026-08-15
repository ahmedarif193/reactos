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
#define RPI5VC4_V3D_WORK_SIZE              (2u * 1024u * 1024u)
#define RPI5VC4_V3D_WORK_GPU_VA            0x01000000u
#define RPI5VC4_V3D_BCL_OFFSET             0x0000u
#define RPI5VC4_V3D_RCL_OFFSET             0x1000u
#define RPI5VC4_V3D_GENERIC_LIST_OFFSET    0x2000u
#define RPI5VC4_V3D_DRAW_DATA_OFFSET       0x3000u
#define RPI5VC4_V3D_TILE_ALLOC_OFFSET      0x4000u
#define RPI5VC4_V3D_TILE_ALLOC_MAX_SIZE    0x4000u
#define RPI5VC4_V3D_TILE_STATE_OFFSET      0x8000u
#define RPI5VC4_V3D_TILE_STATE_MAX_SIZE    0x4000u
#define RPI5VC4_V3D_OUTPUT_OFFSET          0x10000u
#define RPI5VC4_V3D_TILE_WIDTH             64u
#define RPI5VC4_V3D_TILE_HEIGHT            64u
#define RPI5VC4_V3D_SELFTEST_WIDTH         64u
#define RPI5VC4_V3D_SELFTEST_HEIGHT        64u
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
#define V3D71_CL_VERTEX_ARRAY_PRIMS                   36
#define V3D71_CL_SET_INSTANCE_ID                      54
#define V3D71_CL_PRIMITIVE_LIST_FORMAT                56
#define V3D71_CL_GL_SHADER_STATE                      64
#define V3D71_CL_VCM_CACHE_SIZE                       71
#define V3D71_CL_BLEND_ENABLES                        83
#define V3D71_CL_COLOR_WRITE_MASKS                    87
#define V3D71_CL_ZERO_ALL_CENTROID_FLAGS              88
#define V3D71_CL_SAMPLE_STATE                         91
#define V3D71_CL_OCCLUSION_QUERY_COUNTER              92
#define V3D71_CL_CFG_BITS                             96
#define V3D71_CL_ZERO_ALL_FLAT_SHADE_FLAGS            97
#define V3D71_CL_ZERO_ALL_NON_PERSPECTIVE_FLAGS       99
#define V3D71_CL_CLIP_WINDOW                          107
#define V3D71_CL_VIEWPORT_OFFSET                      108
#define V3D71_CL_CLIPPER_Z_MIN_MAX                    109
#define V3D71_CL_CLIPPER_XY_SCALING                   110
#define V3D71_CL_CLIPPER_Z_SCALE_OFFSET               111

#define V3D71_BUFFER_RENDER_TARGET_0 0
#define V3D71_BUFFER_NONE            8
#define V3D71_MEMORY_FORMAT_RASTER   0
#define V3D71_OUTPUT_FORMAT_RGBA8UI  34
#define V3D71_OUTPUT_FORMAT_RGBA8    27
#define V3D71_INTERNAL_BPP_32        0
#define V3D71_RT_TYPE_8              8
#define V3D71_RT_TYPE_8UI_CLAMPED    20
#define V3D71_TILE_SIZE_LOG2_64      3

#define V3D71_PRIMITIVE_TRIANGLES    4
#define V3D71_LIST_TRIANGLES         2
#define V3D71_ATTRIBUTE_FLOAT        2

#define RPI5VC4_V3D_SHADER_RECORD_OFFSET  0x000u
#define RPI5VC4_V3D_ATTRIBUTE0_OFFSET     0x020u
#define RPI5VC4_V3D_ATTRIBUTE1_OFFSET     0x030u
#define RPI5VC4_V3D_BIN_SHADER_OFFSET     0x040u
#define RPI5VC4_V3D_RENDER_SHADER_OFFSET  0x0B8u
#define RPI5VC4_V3D_FRAGMENT_SHADER_OFFSET 0x170u
#define RPI5VC4_V3D_BIN_UNIFORMS_OFFSET   0x1B8u
#define RPI5VC4_V3D_RENDER_UNIFORMS_OFFSET 0x1DCu
#define RPI5VC4_V3D_FRAGMENT_UNIFORMS_OFFSET 0x220u
#define RPI5VC4_V3D_VERTEX_DATA_OFFSET    0x230u

/*
 * Fixed shaders generated offline by Mesa 26.1-devel's V3D 7.1 compiler.
 * The vertex shader passes clip-space position and smooth RGBA color, while
 * the fragment shader writes that color with R/B swapped for the XPDM BGRA
 * back buffer. User mode supplies data only; shader code is never accepted.
 */
static const ULONGLONG Rpi5V3dBinShader[] =
{
    0x39813186bb03f000ULL, 0x39817186bb03f000ULL,
    0x3981e186bc03f140ULL, 0x55824348bc1841c0ULL,
    0x3982e18abc03f240ULL, 0x3840218cbc03f2c0ULL,
    0x544003cebc200320ULL, 0x38002180be03f146ULL,
    0x54000400be34e1c8ULL, 0x54000440be3ce24aULL,
    0x38002192f503f407ULL, 0x38002180be03f2ccULL,
    0x38602180be03f012ULL, 0x38002193f503f447ULL,
    0x38002180be03f013ULL
};

static const ULONGLONG Rpi5V3dRenderShader[] =
{
    0x39813186bb03f000ULL, 0x38403186bb03f000ULL,
    0x38402185bc03f000ULL, 0x54400446bc144000ULL,
    0x38402187bc03f000ULL, 0x39826188bc03f000ULL,
    0x3982e18abc03f240ULL, 0x3983618cbc03f2c0ULL,
    0x3983e18ebc03f340ULL, 0x38402190bc03f3c0ULL,
    0x544004d2bc180220ULL, 0x55860580be1c024aULL,
    0x54400500be4522ccULL, 0x54000540be4d234eULL,
    0x54000657f5592507ULL, 0x38002180be03f3d0ULL,
    0x38402180be03f017ULL, 0x3800219af503f547ULL,
    0x38402180be03f01aULL, 0x3800219b0503f619ULL,
    0x38602180be03f01bULL, 0x38002180be03f012ULL,
    0x38003186bb03f000ULL
};

static const ULONGLONG Rpi5V3dFragmentShader[] =
{
    0x39013186bb03f000ULL, 0x5501d146bb103000ULL,
    0x55228206051c3005ULL, 0x552342c905283008ULL,
    0x5400038c0534300bULL, 0x3800218f0503f00eULL,
    0x382031873503f309ULL, 0x380031873503f18fULL,
    0x38003186bb03f000ULL
};

C_ASSERT(sizeof(Rpi5V3dBinShader) == 0x78);
C_ASSERT(sizeof(Rpi5V3dRenderShader) == 0xB8);
C_ASSERT(sizeof(Rpi5V3dFragmentShader) == 0x48);
C_ASSERT(RPI5VC4_V3D_VERTEX_DATA_OFFSET +
         (3 * sizeof(RPI5VC4_V3D_VERTEX)) <= RPI5VC4_V3D_PAGE_SIZE);

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

static ULONG
Rpi5V3dUnsignedFloatWord(
    _In_ ULONG Value)
{
    ULONG MostSignificantBit = 0;
    ULONG Normalized;

    if (Value == 0)
        return 0;

    Normalized = Value;
    while (Normalized > 1)
    {
        Normalized >>= 1;
        MostSignificantBit++;
    }

    return ((MostSignificantBit + 127u) << 23) |
           ((Value << (23u - MostSignificantBit)) & 0x007FFFFFu);
}

static BOOLEAN
Rpi5V3dFloatFiniteAndBounded(
    _In_ ULONG Value)
{
    ULONG Magnitude = Value & 0x7FFFFFFFu;

    return (Magnitude & 0x7F800000u) != 0x7F800000u &&
           Magnitude <= 0x47800000u; /* 65536.0f */
}

static BOOLEAN
Rpi5V3dColorFloatValid(
    _In_ ULONG Value)
{
    return (Value & 0x80000000u) == 0 && Value <= 0x3F800000u;
}

static BOOLEAN
Rpi5V3dTriangleRequestValid(
    _In_ PRPI5VC4_V3D_TRIANGLE_REQUEST Request)
{
    ULONG Vertex;
    ULONG Component;

    for (Vertex = 0; Vertex < RTL_NUMBER_OF(Request->Vertices); Vertex++)
    {
        for (Component = 0; Component < 4; Component++)
        {
            if (!Rpi5V3dFloatFiniteAndBounded(
                    Request->Vertices[Vertex].Position[Component]) ||
                !Rpi5V3dColorFloatValid(
                    Request->Vertices[Vertex].Color[Component]))
            {
                return FALSE;
            }
        }

        if ((Request->Vertices[Vertex].Position[3] & 0x80000000u) != 0 ||
            (Request->Vertices[Vertex].Position[3] & 0x7FFFFFFFu) == 0)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static VOID
Rpi5V3dClClipWindow(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    Rpi5V3dClByte(Builder, V3D71_CL_CLIP_WINDOW);
    Rpi5V3dClLe16(Builder, 0);
    Rpi5V3dClLe16(Builder, 0);
    Rpi5V3dClLe16(Builder, Width);
    Rpi5V3dClLe16(Builder, Height);
}

static VOID
Rpi5V3dClViewportOffset(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG FineX = Width << 7;  /* (Width / 2) in unsigned 14.8 */
    ULONG FineY = Height << 7; /* (Height / 2) in unsigned 14.8 */

    Rpi5V3dClByte(Builder, V3D71_CL_VIEWPORT_OFFSET);
    Rpi5V3dClByte(Builder, (UCHAR)FineX);
    Rpi5V3dClByte(Builder, (UCHAR)(FineX >> 8));
    Rpi5V3dClByte(Builder, (UCHAR)(FineX >> 16));
    Rpi5V3dClByte(Builder, 0); /* coarse X */
    Rpi5V3dClByte(Builder, (UCHAR)FineY);
    Rpi5V3dClByte(Builder, (UCHAR)(FineY >> 8));
    Rpi5V3dClByte(Builder, (UCHAR)(FineY >> 16));
    Rpi5V3dClByte(Builder, 0); /* coarse Y */
}

static VOID
Rpi5V3dClTriangleState(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Bcl,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG ShaderRecordAddress)
{
    ULONG XyScaleX = Rpi5V3dUnsignedFloatWord(Width << 5);
    ULONG XyScaleY = Rpi5V3dUnsignedFloatWord(Height << 5);

    Rpi5V3dClByte(Bcl, V3D71_CL_CFG_BITS);
    Rpi5V3dClByte(Bcl, 0x03); /* render both primitive orientations */
    Rpi5V3dClByte(Bcl, 0x70); /* depth test always, no depth writes */
    Rpi5V3dClByte(Bcl, 0x40); /* clip Z from -W through W */

    Rpi5V3dClByte(Bcl, V3D71_CL_BLEND_ENABLES);
    Rpi5V3dClByte(Bcl, 0);
    Rpi5V3dClByte(Bcl, V3D71_CL_COLOR_WRITE_MASKS);
    Rpi5V3dClLe32(Bcl, 0); /* zero bits enable all RGBA channels */
    Rpi5V3dClByte(Bcl, V3D71_CL_SAMPLE_STATE);
    Rpi5V3dClByte(Bcl, 0x0F);
    Rpi5V3dClByte(Bcl, 0);
    Rpi5V3dClByte(Bcl, 0x80);
    Rpi5V3dClByte(Bcl, 0x3F); /* coverage 1.0f */

    Rpi5V3dClByte(Bcl, V3D71_CL_ZERO_ALL_FLAT_SHADE_FLAGS);
    Rpi5V3dClByte(Bcl, V3D71_CL_ZERO_ALL_NON_PERSPECTIVE_FLAGS);
    Rpi5V3dClByte(Bcl, V3D71_CL_ZERO_ALL_CENTROID_FLAGS);
    Rpi5V3dClClipWindow(Bcl, Width, Height);

    Rpi5V3dClByte(Bcl, V3D71_CL_CLIPPER_XY_SCALING);
    Rpi5V3dClLe32(Bcl, XyScaleX);
    Rpi5V3dClLe32(Bcl, XyScaleY);
    Rpi5V3dClByte(Bcl, V3D71_CL_CLIPPER_Z_SCALE_OFFSET);
    Rpi5V3dClLe32(Bcl, 0x3F000000u); /* Z scale 0.5f */
    Rpi5V3dClLe32(Bcl, 0x3F000000u); /* Z offset 0.5f */
    Rpi5V3dClByte(Bcl, V3D71_CL_CLIPPER_Z_MIN_MAX);
    Rpi5V3dClLe32(Bcl, 0);
    Rpi5V3dClLe32(Bcl, 0x3F800000u);
    Rpi5V3dClViewportOffset(Bcl, Width, Height);

    Rpi5V3dClByte(Bcl, V3D71_CL_VCM_CACHE_SIZE);
    Rpi5V3dClByte(Bcl, 0x44); /* four batches for bin and render */

    Rpi5V3dClByte(Bcl, V3D71_CL_GL_SHADER_STATE);
    Rpi5V3dClByte(Bcl, (UCHAR)ShaderRecordAddress | 2u);
    Rpi5V3dClByte(Bcl, (UCHAR)(ShaderRecordAddress >> 8));
    Rpi5V3dClByte(Bcl, (UCHAR)(ShaderRecordAddress >> 16));
    Rpi5V3dClByte(Bcl, (UCHAR)(ShaderRecordAddress >> 24));

    Rpi5V3dClByte(Bcl, V3D71_CL_VERTEX_ARRAY_PRIMS);
    Rpi5V3dClByte(Bcl, V3D71_PRIMITIVE_TRIANGLES);
    Rpi5V3dClLe32(Bcl, 3); /* one triangle */
    Rpi5V3dClLe32(Bcl, 0); /* first vertex */
}

static VOID
Rpi5V3dStoreLe32(
    _Out_writes_(4) PUCHAR Destination,
    _In_ ULONG Value)
{
    Destination[0] = (UCHAR)Value;
    Destination[1] = (UCHAR)(Value >> 8);
    Destination[2] = (UCHAR)(Value >> 16);
    Destination[3] = (UCHAR)(Value >> 24);
}

static VOID
Rpi5V3dBuildTriangleData(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_reads_(3) const RPI5VC4_V3D_VERTEX Vertices[3])
{
    PUCHAR Data = (PUCHAR)DeviceExtension->V3dWorkVa +
                  RPI5VC4_V3D_DRAW_DATA_OFFSET;
    ULONG DataGpuVa = RPI5VC4_V3D_WORK_GPU_VA +
                      RPI5VC4_V3D_DRAW_DATA_OFFSET;
    PUCHAR ShaderRecord = Data + RPI5VC4_V3D_SHADER_RECORD_OFFSET;
    PUCHAR Attribute0 = Data + RPI5VC4_V3D_ATTRIBUTE0_OFFSET;
    PUCHAR Attribute1 = Data + RPI5VC4_V3D_ATTRIBUTE1_OFFSET;
    PULONG BinUniforms = (PULONG)(Data +
                                  RPI5VC4_V3D_BIN_UNIFORMS_OFFSET);
    PULONG RenderUniforms = (PULONG)(Data +
                                     RPI5VC4_V3D_RENDER_UNIFORMS_OFFSET);
    ULONG XyScaleX = Rpi5V3dUnsignedFloatWord(Width << 5);
    ULONG XyScaleY = Rpi5V3dUnsignedFloatWord(Height << 5);
    ULONG Index;

    VideoPortZeroMemory(Data, RPI5VC4_V3D_PAGE_SIZE);
    VideoPortMoveMemory(Data + RPI5VC4_V3D_BIN_SHADER_OFFSET,
                        (PVOID)Rpi5V3dBinShader,
                        sizeof(Rpi5V3dBinShader));
    VideoPortMoveMemory(Data + RPI5VC4_V3D_RENDER_SHADER_OFFSET,
                        (PVOID)Rpi5V3dRenderShader,
                        sizeof(Rpi5V3dRenderShader));
    VideoPortMoveMemory(Data + RPI5VC4_V3D_FRAGMENT_SHADER_OFFSET,
                        (PVOID)Rpi5V3dFragmentShader,
                        sizeof(Rpi5V3dFragmentShader));

    /* V3D 7.1.10 draw-index shader record: fixed shaders, four varyings. */
    ShaderRecord[0] = 0x02; /* clipping enabled */
    ShaderRecord[2] = 0x20; /* no implicit point/line varyings */
    ShaderRecord[3] = 4;
    ShaderRecord[4] = 1; /* coordinate output VPM sectors */
    ShaderRecord[5] = 0; /* shared input segment, one required in play */
    ShaderRecord[6] = 1; /* vertex output VPM sectors */
    ShaderRecord[7] = 0; /* shared input segment, one required in play */
    Rpi5V3dStoreLe32(ShaderRecord + 8,
                     DataGpuVa + RPI5VC4_V3D_FRAGMENT_SHADER_OFFSET | 1u);
    Rpi5V3dStoreLe32(ShaderRecord + 12,
                     DataGpuVa + RPI5VC4_V3D_FRAGMENT_UNIFORMS_OFFSET);
    Rpi5V3dStoreLe32(ShaderRecord + 16,
                     DataGpuVa + RPI5VC4_V3D_RENDER_SHADER_OFFSET | 3u);
    Rpi5V3dStoreLe32(ShaderRecord + 20,
                     DataGpuVa + RPI5VC4_V3D_RENDER_UNIFORMS_OFFSET);
    Rpi5V3dStoreLe32(ShaderRecord + 24,
                     DataGpuVa + RPI5VC4_V3D_BIN_SHADER_OFFSET | 3u);
    Rpi5V3dStoreLe32(ShaderRecord + 28,
                     DataGpuVa + RPI5VC4_V3D_BIN_UNIFORMS_OFFSET);

    Rpi5V3dStoreLe32(Attribute0,
                     DataGpuVa + RPI5VC4_V3D_VERTEX_DATA_OFFSET);
    Attribute0[4] = V3D71_ATTRIBUTE_FLOAT << 2; /* vec_size zero means four */
    Attribute0[5] = 0x44; /* four values read by coordinate and vertex */
    Rpi5V3dStoreLe32(Attribute0 + 8, sizeof(RPI5VC4_V3D_VERTEX));
    Rpi5V3dStoreLe32(Attribute0 + 12, 2);

    Rpi5V3dStoreLe32(Attribute1,
                     DataGpuVa + RPI5VC4_V3D_VERTEX_DATA_OFFSET +
                     FIELD_OFFSET(RPI5VC4_V3D_VERTEX, Color));
    Attribute1[4] = V3D71_ATTRIBUTE_FLOAT << 2;
    Attribute1[5] = 0x44;
    Rpi5V3dStoreLe32(Attribute1 + 8, sizeof(RPI5VC4_V3D_VERTEX));
    Rpi5V3dStoreLe32(Attribute1 + 12, 2);

    BinUniforms[0] = XyScaleX;
    for (Index = 0; Index < 4; Index++)
        BinUniforms[1 + Index] = Index;
    BinUniforms[5] = XyScaleY;
    BinUniforms[6] = 4;
    BinUniforms[7] = 5;
    BinUniforms[8] = 0; /* mandatory prefetch slot */

    RenderUniforms[0] = XyScaleX;
    for (Index = 0; Index < 8; Index++)
        RenderUniforms[1 + Index] = Index;
    RenderUniforms[9] = XyScaleY;
    RenderUniforms[10] = 0x3F000000u; /* viewport Z scale */
    RenderUniforms[11] = 0x3F000000u; /* viewport Z offset */
    for (Index = 0; Index < 4; Index++)
        RenderUniforms[12 + Index] = Index;
    RenderUniforms[16] = 0; /* mandatory prefetch slot */

    *(PULONG)(Data + RPI5VC4_V3D_FRAGMENT_UNIFORMS_OFFSET) = 0;
    VideoPortMoveMemory(Data + RPI5VC4_V3D_VERTEX_DATA_OFFSET,
                        (PVOID)Vertices,
                        3 * sizeof(*Vertices));
}

static VOID
Rpi5V3dClRenderingCommon(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG Log2Width = V3D71_TILE_SIZE_LOG2_64;
    ULONG Log2Height = V3D71_TILE_SIZE_LOG2_64;

    Rpi5V3dClByte(Builder, V3D71_CL_TILE_RENDERING_MODE_CFG);
    Rpi5V3dClByte(Builder, 0); /* sub-id 0, one render target minus one */
    Rpi5V3dClLe16(Builder, Width);
    Rpi5V3dClLe16(Builder, Height);
    Rpi5V3dClByte(Builder, (1u << 6)); /* early-z disable */
    Rpi5V3dClByte(Builder,
                  (UCHAR)((Log2Width << 4) | (Log2Height << 7)));
    Rpi5V3dClByte(Builder, (UCHAR)(Log2Height >> 1));
}

static VOID
Rpi5V3dClRenderTargetPart1(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG ClearColor,
    _In_ ULONG RenderTargetType)
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
                          (RenderTargetType << 3)));
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
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Builder,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG TilesX = (Width + RPI5VC4_V3D_TILE_WIDTH - 1) /
                   RPI5VC4_V3D_TILE_WIDTH;
    ULONG TilesY = (Height + RPI5VC4_V3D_TILE_HEIGHT - 1) /
                   RPI5VC4_V3D_TILE_HEIGHT;

    Rpi5V3dClByte(Builder, V3D71_CL_MULTICORE_RENDERING_SUPERTILE_CFG);
    Rpi5V3dClByte(Builder, (UCHAR)(TilesX - 1));
    Rpi5V3dClByte(Builder, (UCHAR)(TilesY - 1));
    Rpi5V3dClByte(Builder, 1); /* frame width in supertiles */
    Rpi5V3dClByte(Builder, 1); /* frame height in supertiles */
    Rpi5V3dClByte(Builder, (UCHAR)TilesX);
    Rpi5V3dClByte(Builder,
                  (UCHAR)((TilesX >> 8) | ((TilesY & 0x0F) << 4)));
    Rpi5V3dClByte(Builder, (UCHAR)(TilesY >> 4));
    Rpi5V3dClByte(Builder, 0); /* one tile list minus one */
}

static VOID
Rpi5V3dBuildBinningList(
    _Inout_ PRPI5VC4_V3D_CL_BUILDER Bcl,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN DrawTriangle,
    _In_ ULONG ShaderRecordAddress)
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
    Rpi5V3dClLe16(Bcl, Width - 1);
    Rpi5V3dClLe16(Bcl, Height - 1);
    Rpi5V3dClByte(Bcl, V3D71_CL_FLUSH_VCD_CACHE);
    if (DrawTriangle)
    {
        Rpi5V3dClByte(Bcl, V3D71_CL_OCCLUSION_QUERY_COUNTER);
        Rpi5V3dClLe32(Bcl, 0);
    }
    Rpi5V3dClByte(Bcl, V3D71_CL_START_TILE_BINNING);
    if (DrawTriangle)
        Rpi5V3dClTriangleState(Bcl, Width, Height, ShaderRecordAddress);
    Rpi5V3dClByte(Bcl, V3D71_CL_FLUSH);
}

static BOOLEAN
Rpi5V3dBuildControlLists(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG ClearColor,
    _In_ BOOLEAN DrawTriangle,
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

    Rpi5V3dBuildBinningList(
        &Bcl,
        Width,
        Height,
        DrawTriangle,
        WorkGpuVa + RPI5VC4_V3D_DRAW_DATA_OFFSET +
        RPI5VC4_V3D_SHADER_RECORD_OFFSET);

    /* Generic per-tile list used by the one 64x64 supertile. */
    Rpi5V3dClByte(&Generic, V3D71_CL_TILE_COORDINATES_IMPLICIT);
    Rpi5V3dClByte(&Generic, V3D71_CL_END_OF_LOADS);
    if (DrawTriangle)
    {
        Rpi5V3dClByte(&Generic, V3D71_CL_PRIMITIVE_LIST_FORMAT);
        Rpi5V3dClByte(&Generic, V3D71_LIST_TRIANGLES);
        Rpi5V3dClByte(&Generic, V3D71_CL_SET_INSTANCE_ID);
        Rpi5V3dClLe32(&Generic, 0);
    }
    Rpi5V3dClByte(&Generic, V3D71_CL_BRANCH_TO_IMPLICIT_TILE_LIST);
    Rpi5V3dClByte(&Generic, 0); /* tile-list set 0 */
    Rpi5V3dClStoreGeneral(&Generic,
                          V3D71_BUFFER_RENDER_TARGET_0,
                          DrawTriangle ? V3D71_OUTPUT_FORMAT_RGBA8 :
                                         V3D71_OUTPUT_FORMAT_RGBA8UI,
                          Width * sizeof(ULONG),
                          WorkGpuVa + RPI5VC4_V3D_OUTPUT_OFFSET);
    if (DrawTriangle)
    {
        /* Do not carry one tile's fragments into the next tile. */
        Rpi5V3dClByte(&Generic, V3D71_CL_CLEAR_RENDER_TARGETS);
    }
    Rpi5V3dClByte(&Generic, V3D71_CL_END_OF_TILE_MARKER);
    Rpi5V3dClByte(&Generic, V3D71_CL_RETURN_FROM_SUB_LIST);
    GenericEnd = GenericStart + Generic.Length;

    /* Mesa's V3D 7.1 render-only buffer-fill RCL, including GFXH-1742. */
    Rpi5V3dClRenderingCommon(&Rcl, Width, Height);
    Rpi5V3dClRenderTargetPart1(
        &Rcl,
        ClearColor,
        DrawTriangle ? V3D71_RT_TYPE_8 : V3D71_RT_TYPE_8UI_CLAMPED);
    Rpi5V3dClZsClear(&Rcl);
    Rpi5V3dClByte(&Rcl, V3D71_CL_TILE_LIST_INITIAL_BLOCK_SIZE);
    Rpi5V3dClByte(&Rcl, 5); /* 128-byte first block, auto-chain enabled */
    Rpi5V3dClByte(&Rcl, V3D71_CL_MULTICORE_RENDERING_TILE_LIST_BASE);
    Rpi5V3dClLe32(&Rcl,
                  WorkGpuVa + RPI5VC4_V3D_TILE_ALLOC_OFFSET);
    Rpi5V3dClSupertileConfig(&Rcl, Width, Height);
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

static ULONG
Rpi5V3dTileAllocationSize(
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG TilesX = (Width + RPI5VC4_V3D_TILE_WIDTH - 1) /
                   RPI5VC4_V3D_TILE_WIDTH;
    ULONG TilesY = (Height + RPI5VC4_V3D_TILE_HEIGHT - 1) /
                   RPI5VC4_V3D_TILE_HEIGHT;
    ULONG InitialBytes = TilesX * TilesY * 128u;

    return ((InitialBytes + RPI5VC4_V3D_PAGE_SIZE - 1) &
            ~(RPI5VC4_V3D_PAGE_SIZE - 1)) +
           (2u * RPI5VC4_V3D_PAGE_SIZE);
}

static VP_STATUS
Rpi5V3dExecuteClear(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG ClearColor,
    _In_reads_opt_(3) const RPI5VC4_V3D_VERTEX Vertices[3],
    _Out_ PRPI5VC4_V3D_SELFTEST Result,
    _Out_writes_bytes_opt_(ReadbackBytes) PVOID Readback,
    _In_ ULONG ReadbackBytes,
    _Out_opt_ PULONG CoveredPixelCount,
    _In_ BOOLEAN DiagnosticLog)
{
    PULONG Output;
    ULONG OutputStride;
    ULONG OutputSize;
    ULONG TileAllocationSize;
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
    BOOLEAN DrawTriangle = Vertices != NULL;

    VideoPortZeroMemory(Result, sizeof(*Result));
    Result->Size = sizeof(*Result);
    Result->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_NOT_SUPPORTED;
    Result->ExpectedPixel = ClearColor;
    if (CoveredPixelCount != NULL)
        *CoveredPixelCount = 0;

    if (Width == 0 || Height == 0 ||
        Width > RPI5VC4_V3D_CLEAR_MAX_WIDTH ||
        Height > RPI5VC4_V3D_CLEAR_MAX_HEIGHT)
    {
        return ERROR_INVALID_PARAMETER;
    }

    OutputStride = Width * sizeof(ULONG);
    OutputSize = OutputStride * Height;
    TileAllocationSize = Rpi5V3dTileAllocationSize(Width, Height);
    if (RPI5VC4_V3D_OUTPUT_OFFSET + OutputSize > RPI5VC4_V3D_WORK_SIZE ||
        TileAllocationSize > RPI5VC4_V3D_TILE_ALLOC_MAX_SIZE ||
        (((Width + RPI5VC4_V3D_TILE_WIDTH - 1) /
          RPI5VC4_V3D_TILE_WIDTH) *
         ((Height + RPI5VC4_V3D_TILE_HEIGHT - 1) /
          RPI5VC4_V3D_TILE_HEIGHT) * 256u) >
            RPI5VC4_V3D_TILE_STATE_MAX_SIZE ||
        (Readback != NULL && ReadbackBytes < OutputSize))
    {
        return ERROR_INVALID_PARAMETER;
    }

    if (InterlockedCompareExchange(&DeviceExtension->V3dExecutionBusy,
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
         Poll < OutputSize / sizeof(ULONG);
         Poll++)
    {
        Output[Poll] = RPI5VC4_V3D_SENTINEL_PIXEL;
    }

    if (DrawTriangle)
        Rpi5V3dBuildTriangleData(DeviceExtension, Width, Height, Vertices);

    if (!Rpi5V3dBuildControlLists(DeviceExtension,
                                  Width,
                                  Height,
                                  ClearColor,
                                  DrawTriangle,
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
                 TileAllocationSize);
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
        Output[(Height / 2) * Width + (Width / 2)];
    Result->LastPixel =
        Output[Width * Height - 1];
    for (Poll = 0; Poll < OutputSize / sizeof(ULONG); Poll++)
    {
        if (DrawTriangle)
        {
            if (CoveredPixelCount != NULL && Output[Poll] != ClearColor)
                (*CoveredPixelCount)++;
        }
        else if (Output[Poll] != ClearColor)
        {
            Result->MismatchCount++;
        }
    }
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_READBACK_VALID;

    if (!DrawTriangle && Result->MismatchCount != 0)
    {
        Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_READBACK_MISMATCH;
        goto Capture;
    }

    Result->Status = RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS;
    Result->Flags |= RPI5VC4_V3D_SELFTEST_FLAG_PASSED;
    if (Readback != NULL)
        VideoPortMoveMemory(Readback, Output, OutputSize);

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

    if (DiagnosticLog)
    {
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
    }

Done:
    InterlockedExchange(&DeviceExtension->V3dExecutionBusy, 0);
    return NO_ERROR;
}

VP_STATUS
Rpi5V3dRunSelfTest(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PRPI5VC4_V3D_SELFTEST Result)
{
    return Rpi5V3dExecuteClear(DeviceExtension,
                               RPI5VC4_V3D_SELFTEST_WIDTH,
                               RPI5VC4_V3D_SELFTEST_HEIGHT,
                               RPI5VC4_V3D_EXPECTED_PIXEL,
                               NULL,
                               Result,
                               NULL,
                               0,
                               NULL,
                               TRUE);
}

VP_STATUS
Rpi5V3dRenderClear(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PRPI5VC4_V3D_CLEAR_REQUEST Request,
    _Out_writes_bytes_(ResultBufferLength) PRPI5VC4_V3D_CLEAR_RESULT Result,
    _In_ ULONG ResultBufferLength,
    _Out_ PULONG BytesReturned)
{
    RPI5VC4_V3D_SELFTEST Diagnostics;
    ULONG HeaderSize = FIELD_OFFSET(RPI5VC4_V3D_CLEAR_RESULT, Pixels);
    ULONG PixelBytes;
    ULONG RequiredSize;
    ULONG Width;
    ULONG Height;
    ULONG ClearColor;
    VP_STATUS Status;

    *BytesReturned = 0;
    if (Request->Size < sizeof(*Request) ||
        Request->AbiVersion != RPI5VC4_XPDM_ABI_VERSION ||
        Request->Width == 0 ||
        Request->Width > RPI5VC4_V3D_CLEAR_MAX_WIDTH ||
        Request->Height == 0 ||
        Request->Height > RPI5VC4_V3D_CLEAR_MAX_HEIGHT)
    {
        return ERROR_INVALID_PARAMETER;
    }

    Width = Request->Width;
    Height = Request->Height;
    ClearColor = Request->ClearColor;
    PixelBytes = Width * Height * sizeof(ULONG);
    RequiredSize = HeaderSize + PixelBytes;
    if (ResultBufferLength < RequiredSize)
        return ERROR_INSUFFICIENT_BUFFER;

    Status = Rpi5V3dExecuteClear(DeviceExtension,
                                 Width,
                                 Height,
                                 ClearColor,
                                 NULL,
                                 &Diagnostics,
                                 Result->Pixels,
                                 PixelBytes,
                                 NULL,
                                 FALSE);
    if (Status != NO_ERROR)
        return Status;

    Result->Size = RequiredSize;
    Result->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Result->Status = Diagnostics.Status;
    Result->Flags = Diagnostics.Flags;
    Result->Width = Width;
    Result->Height = Height;
    Result->Stride = Width * sizeof(ULONG);
    Result->ClearColor = ClearColor;
    Result->PixelBytes = Diagnostics.Status ==
                         RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS ?
                         PixelBytes : 0;
    Result->Diagnostics = Diagnostics;
    *BytesReturned = Diagnostics.Status ==
                     RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS ?
                     RequiredSize : HeaderSize;
    return NO_ERROR;
}

VP_STATUS
Rpi5V3dRenderTriangle(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PRPI5VC4_V3D_TRIANGLE_REQUEST Request,
    _Out_writes_bytes_(ResultBufferLength) PRPI5VC4_V3D_TRIANGLE_RESULT Result,
    _In_ ULONG ResultBufferLength,
    _Out_ PULONG BytesReturned)
{
    RPI5VC4_V3D_SELFTEST Diagnostics;
    RPI5VC4_V3D_VERTEX Vertices[3];
    ULONG HeaderSize = FIELD_OFFSET(RPI5VC4_V3D_TRIANGLE_RESULT, Pixels);
    ULONG PixelBytes;
    ULONG RequiredSize;
    ULONG CoveredPixelCount = 0;
    ULONG Width;
    ULONG Height;
    ULONG ClearColor;
    VP_STATUS Status;

    *BytesReturned = 0;
    if (Request->Size < sizeof(*Request) ||
        Request->AbiVersion != RPI5VC4_XPDM_ABI_VERSION ||
        Request->Width == 0 ||
        Request->Width > RPI5VC4_V3D_CLEAR_MAX_WIDTH ||
        Request->Height == 0 ||
        Request->Height > RPI5VC4_V3D_CLEAR_MAX_HEIGHT ||
        !Rpi5V3dTriangleRequestValid(Request))
    {
        return ERROR_INVALID_PARAMETER;
    }

    Width = Request->Width;
    Height = Request->Height;
    ClearColor = Request->ClearColor;
    VideoPortMoveMemory(Vertices,
                        Request->Vertices,
                        sizeof(Vertices));
    PixelBytes = Width * Height * sizeof(ULONG);
    RequiredSize = HeaderSize + PixelBytes;
    if (ResultBufferLength < RequiredSize)
        return ERROR_INSUFFICIENT_BUFFER;

    Status = Rpi5V3dExecuteClear(DeviceExtension,
                                 Width,
                                 Height,
                                 ClearColor,
                                 Vertices,
                                 &Diagnostics,
                                 Result->Pixels,
                                 PixelBytes,
                                 &CoveredPixelCount,
                                 FALSE);
    if (Status != NO_ERROR)
        return Status;

    Result->Size = RequiredSize;
    Result->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    Result->Status = Diagnostics.Status;
    Result->Flags = Diagnostics.Flags;
    Result->Width = Width;
    Result->Height = Height;
    Result->Stride = Width * sizeof(ULONG);
    Result->ClearColor = ClearColor;
    Result->PixelBytes = Diagnostics.Status ==
                         RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS ?
                         PixelBytes : 0;
    Result->CoveredPixelCount = CoveredPixelCount;
    Result->Diagnostics = Diagnostics;
    *BytesReturned = Diagnostics.Status ==
                     RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS ?
                     RequiredSize : HeaderSize;
    return NO_ERROR;
}
