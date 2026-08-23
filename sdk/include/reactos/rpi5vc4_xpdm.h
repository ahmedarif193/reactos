/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared private interface for the RPi5 display miniport and ICD
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _REACTOS_RPI5VC4_XPDM_H_
#define _REACTOS_RPI5VC4_XPDM_H_

#define IOCTL_VIDEO_RPI5VC4_LATCH_SCANOUT \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x830, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_QUERY_V3D \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x831, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_RUN_V3D_SELFTEST \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x832, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_RENDER_CLEAR \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x833, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_RENDER_TRIANGLE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x834, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_RENDER_BATCH \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x835, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_UPLOAD_TEXTURE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x836, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_RENDER_GRAPH \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x837, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_READ_GRAPH \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x838, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_PRESENT_GDI \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x839, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_RPI5VC4_QUERY_PLATFORM \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x83A, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define RPI5VC4_ESCAPE_QUERY_V3D 0x52505633 /* "RPV3" */
#define RPI5VC4_ESCAPE_RUN_V3D_SELFTEST 0x52505654 /* "RPVT" */
#define RPI5VC4_ESCAPE_RENDER_CLEAR 0x5250434C /* "RPCL" */
#define RPI5VC4_ESCAPE_RENDER_TRIANGLE 0x52505452 /* "RPTR" */
#define RPI5VC4_ESCAPE_RENDER_BATCH 0x52504254 /* "RPBT" */
#define RPI5VC4_ESCAPE_UPLOAD_TEXTURE 0x52505458 /* "RPTX" */
#define RPI5VC4_ESCAPE_RENDER_GRAPH 0x52504752 /* "RPGR" */
#define RPI5VC4_ESCAPE_READ_GRAPH 0x52504744 /* "RPGD" */
#define RPI5VC4_ESCAPE_QUERY_PLATFORM 0x52505049 /* "RPPI" */

#define RPI5VC4_XPDM_ABI_VERSION 8

#define RPI5VC4_PLATFORM_MAX_THERMAL_ZONES 8u
#define RPI5VC4_PLATFORM_MAX_FANS          8u

#define RPI5VC4_THERMAL_FLAG_PRESENT          (1u << 0)
#define RPI5VC4_THERMAL_FLAG_PASSIVE_TRIP     (1u << 1)
#define RPI5VC4_THERMAL_FLAG_CRITICAL_TRIP    (1u << 2)
#define RPI5VC4_THERMAL_FLAG_THERMAL_THROTTLE (1u << 3)

#define RPI5VC4_FAN_FLAG_PRESENT       (1u << 0)
#define RPI5VC4_FAN_FLAG_CONTROL_VALID (1u << 1)
#define RPI5VC4_FAN_FLAG_SPEED_VALID   (1u << 2)

/*
 * Platform telemetry is collected through the standard ACPI thermal-zone and
 * fan device interfaces. Temperatures use the ACPI unit of tenths Kelvin;
 * fan speed is RPM when firmware reports it through _FST.
 */
typedef struct _RPI5VC4_THERMAL_ZONE_INFO
{
    ULONG Flags;
    ULONG CurrentTemperature;
    ULONG PassiveTripPoint;
    ULONG CriticalTripPoint;
    ULONG Reserved[4];
} RPI5VC4_THERMAL_ZONE_INFO, *PRPI5VC4_THERMAL_ZONE_INFO;

typedef struct _RPI5VC4_FAN_INFO
{
    ULONG Flags;
    ULONG Control;
    ULONG Speed;
    ULONG Reserved[5];
} RPI5VC4_FAN_INFO, *PRPI5VC4_FAN_INFO;

typedef struct _RPI5VC4_PLATFORM_INFO
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG ThermalZoneCount;
    ULONG FanCount;
    RPI5VC4_THERMAL_ZONE_INFO ThermalZones[RPI5VC4_PLATFORM_MAX_THERMAL_ZONES];
    RPI5VC4_FAN_INFO Fans[RPI5VC4_PLATFORM_MAX_FANS];
    ULONG Reserved[8];
} RPI5VC4_PLATFORM_INFO, *PRPI5VC4_PLATFORM_INFO;

#define RPI5VC4_V3D_FLAG_SMS_MAPPED  (1u << 0)
#define RPI5VC4_V3D_FLAG_POWERED     (1u << 1)
#define RPI5VC4_V3D_FLAG_REGS_MAPPED (1u << 2)
#define RPI5VC4_V3D_FLAG_IDENT_VALID (1u << 3)

typedef struct _RPI5VC4_V3D_INFO
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Flags;
    ULONG Version;
    ULONG CoreCount;
    ULONG SmsReeCs;
    ULONG SmsTeeCs;
    ULONG HubIdent[4];
    ULONG CoreIdent[3];
    ULONG MmuDebugInfo;
    ULONG Reserved[8];
} RPI5VC4_V3D_INFO, *PRPI5VC4_V3D_INFO;

#define RPI5VC4_V3D_SELFTEST_FLAG_DMA_ALLOCATED  (1u << 0)
#define RPI5VC4_V3D_SELFTEST_FLAG_MMU_PROGRAMMED (1u << 1)
#define RPI5VC4_V3D_SELFTEST_FLAG_JOB_KICKED     (1u << 2)
#define RPI5VC4_V3D_SELFTEST_FLAG_JOB_COMPLETED  (1u << 3)
#define RPI5VC4_V3D_SELFTEST_FLAG_READBACK_VALID (1u << 4)
#define RPI5VC4_V3D_SELFTEST_FLAG_PASSED         (1u << 5)
#define RPI5VC4_V3D_SELFTEST_FLAG_BINNING_KICKED (1u << 6)
#define RPI5VC4_V3D_SELFTEST_FLAG_BINNING_DONE   (1u << 7)

#define RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS             0
#define RPI5VC4_V3D_SELFTEST_STATUS_NOT_SUPPORTED       1
#define RPI5VC4_V3D_SELFTEST_STATUS_BUSY                2
#define RPI5VC4_V3D_SELFTEST_STATUS_ALLOCATION_FAILED   3
#define RPI5VC4_V3D_SELFTEST_STATUS_ADDRESS_UNSUPPORTED 4
#define RPI5VC4_V3D_SELFTEST_STATUS_ENGINE_BUSY         5
#define RPI5VC4_V3D_SELFTEST_STATUS_MMU_TIMEOUT         6
#define RPI5VC4_V3D_SELFTEST_STATUS_RENDER_TIMEOUT      7
#define RPI5VC4_V3D_SELFTEST_STATUS_MMU_FAULT           8
#define RPI5VC4_V3D_SELFTEST_STATUS_RENDER_ERROR        9
#define RPI5VC4_V3D_SELFTEST_STATUS_READBACK_MISMATCH  10
#define RPI5VC4_V3D_SELFTEST_STATUS_POISONED           11
#define RPI5VC4_V3D_SELFTEST_STATUS_BINNING_TIMEOUT    12
#define RPI5VC4_V3D_SELFTEST_STATUS_BINNING_ERROR      13
#define RPI5VC4_V3D_SELFTEST_STATUS_TEXTURE_MISMATCH   14
#define RPI5VC4_V3D_SELFTEST_STATUS_TFU_TIMEOUT        15
#define RPI5VC4_V3D_SELFTEST_STATUS_TFU_ERROR          16

/*
 * Result of a kernel-built, fixed 64x64 V3D render-and-readback job.  The
 * interface deliberately exposes neither MMIO nor arbitrary command-list
 * submission; it is the first bounded hardware boundary used by the XPDM
 * OpenGL bring-up.
 */
typedef struct _RPI5VC4_V3D_SELFTEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Status;
    ULONG Flags;
    ULONG ExpectedPixel;
    ULONG FirstPixel;
    ULONG CenterPixel;
    ULONG LastPixel;
    ULONG RfcBefore;
    ULONG RfcAfter;
    ULONG CoreInterruptStatus;
    ULONG Ct1Current;
    ULONG Ct1End;
    ULONG MmuControl;
    ULONG MmuViolationAddress;
    ULONG ErrorStatus;
    ULONG PollCount;
    ULONG RenderControlListBytes;
    ULONG GenericTileListBytes;
    ULONG MismatchCount;
    ULONG BfcBefore;
    ULONG BfcAfter;
    ULONG Ct0Current;
    ULONG Ct0End;
    ULONG BinningPollCount;
    ULONG BinningControlListBytes;
    ULONG Reserved;
} RPI5VC4_V3D_SELFTEST, *PRPI5VC4_V3D_SELFTEST;

#define RPI5VC4_V3D_CLEAR_MAX_WIDTH  1024u
#define RPI5VC4_V3D_CLEAR_MAX_HEIGHT 768u

/*
 * A clear request is deliberately higher-level than a V3D command list.
 * The miniport validates the dimensions and builds every GPU packet itself.
 * ClearColor is an 0xAARRGGBB value; result rows use the OpenGL bottom-left
 * origin and can therefore be consumed directly as a bottom-up 32-bpp DIB.
 */
typedef struct _RPI5VC4_V3D_CLEAR_REQUEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Width;
    ULONG Height;
    ULONG ClearColor;
    ULONG Reserved[3];
} RPI5VC4_V3D_CLEAR_REQUEST, *PRPI5VC4_V3D_CLEAR_REQUEST;

typedef struct _RPI5VC4_V3D_CLEAR_RESULT
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Status;
    ULONG Flags;
    ULONG Width;
    ULONG Height;
    ULONG Stride;
    ULONG ClearColor;
    ULONG PixelBytes;
    RPI5VC4_V3D_SELFTEST Diagnostics;
    ULONG Pixels[1];
} RPI5VC4_V3D_CLEAR_RESULT, *PRPI5VC4_V3D_CLEAR_RESULT;

/*
 * A primitive request carries one bounded triangle or triangle strip with
 * clip-space positions and RGBA colors. The miniport supplies fixed V3D 7.1
 * shaders and builds every state, attribute, binning, and rendering packet.
 * IEEE-754 values are represented as raw words so the kernel never enters a
 * floating-point context.
 */
#define RPI5VC4_V3D_PRIMITIVE_TRIANGLES     1u
#define RPI5VC4_V3D_PRIMITIVE_TRIANGLE_STRIP 2u
#define RPI5VC4_V3D_PRIMITIVE_MAX_VERTICES 4u

typedef struct _RPI5VC4_V3D_VERTEX
{
    ULONG Position[4];
    ULONG Color[4];
    ULONG TexCoord[2];
} RPI5VC4_V3D_VERTEX, *PRPI5VC4_V3D_VERTEX;

/*
 * These values are the bounded, API-independent blend operations accepted by
 * the miniport. They intentionally mirror the V3D 7.1 blend packet encoding,
 * but applications still cannot provide a packet or a command list.
 */
#define RPI5VC4_V3D_BLEND_FLAG_ENABLE (1u << 0)

#define RPI5VC4_V3D_BLEND_EQUATION_ADD              0u
#define RPI5VC4_V3D_BLEND_EQUATION_SUBTRACT         1u
#define RPI5VC4_V3D_BLEND_EQUATION_REVERSE_SUBTRACT 2u
#define RPI5VC4_V3D_BLEND_EQUATION_MINIMUM          3u
#define RPI5VC4_V3D_BLEND_EQUATION_MAXIMUM          4u

#define RPI5VC4_V3D_BLEND_FACTOR_ZERO                     0u
#define RPI5VC4_V3D_BLEND_FACTOR_ONE                      1u
#define RPI5VC4_V3D_BLEND_FACTOR_SOURCE_COLOR             2u
#define RPI5VC4_V3D_BLEND_FACTOR_ONE_MINUS_SOURCE_COLOR   3u
#define RPI5VC4_V3D_BLEND_FACTOR_DESTINATION_COLOR        4u
#define RPI5VC4_V3D_BLEND_FACTOR_ONE_MINUS_DESTINATION_COLOR 5u
#define RPI5VC4_V3D_BLEND_FACTOR_SOURCE_ALPHA             6u
#define RPI5VC4_V3D_BLEND_FACTOR_ONE_MINUS_SOURCE_ALPHA   7u
#define RPI5VC4_V3D_BLEND_FACTOR_DESTINATION_ALPHA        8u
#define RPI5VC4_V3D_BLEND_FACTOR_ONE_MINUS_DESTINATION_ALPHA 9u
#define RPI5VC4_V3D_BLEND_FACTOR_CONSTANT_COLOR          10u
#define RPI5VC4_V3D_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR 11u
#define RPI5VC4_V3D_BLEND_FACTOR_CONSTANT_ALPHA          12u
#define RPI5VC4_V3D_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA 13u
#define RPI5VC4_V3D_BLEND_FACTOR_SOURCE_ALPHA_SATURATE   14u

typedef struct _RPI5VC4_V3D_BLEND_STATE
{
    ULONG Flags;
    ULONG ColorEquation;
    ULONG AlphaEquation;
    ULONG SourceColorFactor;
    ULONG DestinationColorFactor;
    ULONG SourceAlphaFactor;
    ULONG DestinationAlphaFactor;
    /* IEEE-754 binary16 values packed as R|G and B|A, low component first. */
    ULONG ConstantColorLow;
    ULONG ConstantColorHigh;
} RPI5VC4_V3D_BLEND_STATE, *PRPI5VC4_V3D_BLEND_STATE;

typedef struct _RPI5VC4_V3D_TRIANGLE_REQUEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Width;
    ULONG Height;
    ULONG ClearColor;
    ULONG PrimitiveType;
    ULONG VertexCount;
    RPI5VC4_V3D_BLEND_STATE BlendState;
    RPI5VC4_V3D_VERTEX Vertices[RPI5VC4_V3D_PRIMITIVE_MAX_VERTICES];
} RPI5VC4_V3D_TRIANGLE_REQUEST, *PRPI5VC4_V3D_TRIANGLE_REQUEST;

typedef struct _RPI5VC4_V3D_TRIANGLE_RESULT
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Status;
    ULONG Flags;
    ULONG Width;
    ULONG Height;
    ULONG Stride;
    ULONG ClearColor;
    ULONG PixelBytes;
    ULONG CoveredPixelCount;
    ULONG PrimitiveType;
    ULONG VertexCount;
    RPI5VC4_V3D_SELFTEST Diagnostics;
    ULONG Pixels[1];
} RPI5VC4_V3D_TRIANGLE_RESULT, *PRPI5VC4_V3D_TRIANGLE_RESULT;

/*
 * A batch contains Mesa-transformed, clipped, and culled triangles. The
 * miniport still owns the fixed shaders and every V3D packet; user mode can
 * provide only bounded vertex data and the supported depth-test state.
 */
#define RPI5VC4_V3D_BATCH_MAX_VERTICES 144000u
#define RPI5VC4_V3D_BATCH_FLAG_DEPTH_TEST  (1u << 0)
#define RPI5VC4_V3D_BATCH_FLAG_DEPTH_WRITE (1u << 1)
#define RPI5VC4_V3D_BATCH_FLAG_DIRECT_PRESENT (1u << 2)
#define RPI5VC4_V3D_BATCH_FLAG_DEPTH_LEQUAL (1u << 3)
#define RPI5VC4_V3D_BATCH_FLAG_TEXTURED (1u << 4)
#define RPI5VC4_V3D_BATCH_FLAG_TEXTURE_LINEAR (1u << 5)
#define RPI5VC4_V3D_BATCH_FLAG_TEXTURE_MIPMAP (1u << 6)
#define RPI5VC4_V3D_BATCH_FLAG_BLINN_PHONG (1u << 7)
#define RPI5VC4_V3D_BATCH_FLAG_PHONG (1u << 8)
#define RPI5VC4_V3D_BATCH_FLAG_CEL (1u << 9)
#define RPI5VC4_V3D_BATCH_FLAG_BUMP_POLY (1u << 10)
#define RPI5VC4_V3D_BATCH_FLAG_NORMAL_MAP (1u << 11)
#define RPI5VC4_V3D_BATCH_FLAG_HEIGHT_MAP (1u << 12)
#define RPI5VC4_V3D_BATCH_FLAG_EFFECT_EDGE (1u << 13)
#define RPI5VC4_V3D_BATCH_FLAG_EFFECT_BLUR (1u << 14)
#define RPI5VC4_V3D_BATCH_FLAG_BLEND_PRESERVE_ALPHA (1u << 15)
#define RPI5VC4_V3D_BATCH_FLAG_DESKTOP_BLUR_HORIZONTAL (1u << 16)
#define RPI5VC4_V3D_BATCH_FLAG_DESKTOP_BLUR_VERTICAL   (1u << 17)
#define RPI5VC4_V3D_BATCH_FLAG_OUTPUT_DEPTH            (1u << 18)
#define RPI5VC4_V3D_BATCH_FLAG_BLEND_STANDARD           (1u << 19)
#define RPI5VC4_V3D_BATCH_FLAG_SHADOW                   (1u << 20)
#define RPI5VC4_V3D_BATCH_FLAG_WIREFRAME               (1u << 21)
#define RPI5VC4_V3D_BATCH_FLAG_IDEAS                    (1u << 22)
#define RPI5VC4_V3D_BATCH_FLAG_JELLYFISH                (1u << 23)
#define RPI5VC4_V3D_NORMAL_MATRIX_WORDS 9u
#define RPI5VC4_V3D_HEIGHT_MAX_VERTICES 4096u
#define RPI5VC4_V3D_JELLYFISH_MAX_VERTICES 65536u
#define RPI5VC4_V3D_BATCH_MAX_DRAWS 8u
#define RPI5VC4_V3D_IDEAS_UNIFORM_WORDS 16u
#define RPI5VC4_V3D_IDEAS_MODE_COLOR  0u
#define RPI5VC4_V3D_IDEAS_MODE_SHADOW 1u
#define RPI5VC4_V3D_IDEAS_MODE_LOGO   2u
#define RPI5VC4_V3D_IDEAS_MODE_LAMP   3u
#define RPI5VC4_V3D_IDEAS_MODE_COUNT  4u
#define RPI5VC4_V3D_JELLYFISH_UNIFORM_WORDS 6u
#define RPI5VC4_V3D_JELLYFISH_MODE_GRADIENT 0u
#define RPI5VC4_V3D_JELLYFISH_MODE_MESH     1u
#define RPI5VC4_V3D_JELLYFISH_MODE_COUNT    2u
#define RPI5VC4_V3D_SHADOW_MODE_GROUND       0u
#define RPI5VC4_V3D_SHADOW_MODE_COLOR        1u
#define RPI5VC4_V3D_SHADOW_MODE_COUNT        2u

typedef struct _RPI5VC4_V3D_BATCH_DRAW
{
    ULONG FirstVertex;
    ULONG VertexCount;
    ULONG Flags;
    ULONG ShaderMode;
} RPI5VC4_V3D_BATCH_DRAW, *PRPI5VC4_V3D_BATCH_DRAW;

typedef struct _RPI5VC4_V3D_TEXCOORD
{
    ULONG Component[2];
} RPI5VC4_V3D_TEXCOORD, *PRPI5VC4_V3D_TEXCOORD;

#define RPI5VC4_V3D_TEXTURE_MAX_WIDTH  1024u
#define RPI5VC4_V3D_TEXTURE_MAX_HEIGHT 1024u
#define RPI5VC4_V3D_TEXTURE_MAX_LEVELS 11u
#define RPI5VC4_V3D_TEXTURE_FORMAT_RGBA8 1u
#define RPI5VC4_V3D_TEXTURE_FORMAT_D24_X8 2u
#define RPI5VC4_V3D_TEXTURE_SLOT_COUNT 2u
#define RPI5VC4_V3D_TEXTURE_SLOT_PRIMARY 0u
#define RPI5VC4_V3D_TEXTURE_SLOT_SECONDARY 1u

typedef struct _RPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Width;
    ULONG Height;
    ULONG Format;
    ULONG RowStride;
    ULONG PixelBytes;
    ULONG LevelCount;
    ULONG TextureSlot;
    UCHAR Pixels[1];
} RPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST,
  *PRPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST;

typedef struct _RPI5VC4_V3D_TEXTURE_UPLOAD_RESULT
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Status;
    ULONG Generation;
    ULONG Width;
    ULONG Height;
    ULONG Format;
    ULONG LevelCount;
    ULONG TextureSlot;
} RPI5VC4_V3D_TEXTURE_UPLOAD_RESULT,
  *PRPI5VC4_V3D_TEXTURE_UPLOAD_RESULT;

typedef struct _RPI5VC4_V3D_BATCH_REQUEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Width;
    ULONG Height;
    ULONG ClearColor;
    ULONG Flags;
    ULONG VertexCount;
    ULONG DestinationX;
    ULONG DestinationY;
    ULONG TextureGeneration;
    ULONG TextureGeneration1;
    ULONG NormalMatrix[RPI5VC4_V3D_NORMAL_MATRIX_WORDS];
    RPI5VC4_V3D_BLEND_STATE BlendState;
    ULONG DrawCount;
    RPI5VC4_V3D_BATCH_DRAW Draws[RPI5VC4_V3D_BATCH_MAX_DRAWS];
    ULONG ShaderUniforms[RPI5VC4_V3D_IDEAS_UNIFORM_WORDS];
    /* Height-map UVs, when present, immediately follow VertexCount vertices. */
    RPI5VC4_V3D_VERTEX Vertices[1];
} RPI5VC4_V3D_BATCH_REQUEST, *PRPI5VC4_V3D_BATCH_REQUEST;

typedef struct _RPI5VC4_V3D_BATCH_RESULT
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Status;
    ULONG Flags;
    ULONG Width;
    ULONG Height;
    ULONG Stride;
    ULONG ClearColor;
    ULONG PixelBytes;
    ULONG VertexCount;
    ULONG DestinationX;
    ULONG DestinationY;
    RPI5VC4_V3D_SELFTEST Diagnostics;
    ULONG Pixels[1];
} RPI5VC4_V3D_BATCH_RESULT, *PRPI5VC4_V3D_BATCH_RESULT;

/*
 * A render graph keeps a bounded set of texture-backed render targets in the
 * miniport-owned V3D work buffer and executes an ordered list of fixed-shader
 * passes without returning intermediate images to user mode.  User mode still
 * supplies only validated vertices, texture pixels, and high-level state; it
 * cannot submit V3D command lists or GPU addresses.
 */
#define RPI5VC4_V3D_GRAPH_MAX_RESOURCES 12u
#define RPI5VC4_V3D_GRAPH_MAX_PASSES 32u
#define RPI5VC4_V3D_GRAPH_MAX_VERTICES 6u
#define RPI5VC4_V3D_GRAPH_MAX_SOURCES 6u
#define RPI5VC4_V3D_GRAPH_DEFAULT_TARGET 0xFFFFFFFFu

#define RPI5VC4_V3D_GRAPH_SHADER_FIXED_TEXTURE 0u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NOISE 1u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_NORMAL 2u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_LUMINANCE 3u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_OVERLAY 4u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_BLOOM_HORIZONTAL 5u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_BLOOM_VERTICAL 6u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_HORIZONTAL 7u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN_TILT_VERTICAL 8u
#define RPI5VC4_V3D_GRAPH_SHADER_TERRAIN 9u
#define RPI5VC4_V3D_GRAPH_SHADER_COUNT 10u

#define RPI5VC4_V3D_TERRAIN_GRID_SIDE 257u
#define RPI5VC4_V3D_TERRAIN_VERTEX_COUNT \
    (RPI5VC4_V3D_TERRAIN_GRID_SIDE * RPI5VC4_V3D_TERRAIN_GRID_SIDE)
#define RPI5VC4_V3D_TERRAIN_INDEX_COUNT (256u * 256u * 6u)
#define RPI5VC4_V3D_TERRAIN_VERTEX_UNIFORM_WORDS 48u
#define RPI5VC4_V3D_TERRAIN_FRAGMENT_UNIFORM_WORDS 5u
#define RPI5VC4_V3D_TERRAIN_UNIFORM_WORDS \
    (RPI5VC4_V3D_TERRAIN_VERTEX_UNIFORM_WORDS + \
     RPI5VC4_V3D_TERRAIN_FRAGMENT_UNIFORM_WORDS)

#define RPI5VC4_V3D_GRAPH_REQUEST_INITIALIZE (1u << 0)
#define RPI5VC4_V3D_GRAPH_RESOURCE_INITIAL_DATA (1u << 0)
#define RPI5VC4_V3D_GRAPH_SOURCE_LINEAR (1u << 0)
#define RPI5VC4_V3D_GRAPH_SOURCE_MIPMAP (1u << 1)
#define RPI5VC4_V3D_GRAPH_SOURCE_REPEAT_S (1u << 2)
#define RPI5VC4_V3D_GRAPH_SOURCE_REPEAT_T (1u << 3)
#define RPI5VC4_V3D_GRAPH_PASS_LOAD_TARGET (1u << 18)
#define RPI5VC4_V3D_GRAPH_PASS_BLEND_STANDARD (1u << 19)
#define RPI5VC4_V3D_GRAPH_PASS_BLEND_PRESERVE_ALPHA (1u << 20)
#define RPI5VC4_V3D_GRAPH_PASS_BLEND_ADDITIVE (1u << 24)
#define RPI5VC4_V3D_GRAPH_PASS_GENERATE_MIPMAP (1u << 25)
#define RPI5VC4_V3D_GRAPH_RESULT_CACHE_INITIALIZED (1u << 0)
#define RPI5VC4_V3D_GRAPH_RESULT_DIRECT_PRESENTED (1u << 1)

typedef struct _RPI5VC4_V3D_GRAPH_RESOURCE
{
    ULONG Width;
    ULONG Height;
    ULONG Format;
    ULONG Flags;
    ULONG RowStride;
    ULONG PixelBytes;
    ULONG PixelOffset;
    ULONG LevelCount;
} RPI5VC4_V3D_GRAPH_RESOURCE, *PRPI5VC4_V3D_GRAPH_RESOURCE;

typedef struct _RPI5VC4_V3D_TERRAIN_VERTEX
{
    ULONG Position[3];
    ULONG Normal[3];
    ULONG Tangent[3];
    ULONG TexCoord[2];
} RPI5VC4_V3D_TERRAIN_VERTEX, *PRPI5VC4_V3D_TERRAIN_VERTEX;

typedef struct _RPI5VC4_V3D_GRAPH_PASS
{
    ULONG TargetResource;
    ULONG SourceResource;
    ULONG SourceCount;
    ULONG ShaderMode;
    ULONG SourceResources[RPI5VC4_V3D_GRAPH_MAX_SOURCES];
    ULONG SourceFlags[RPI5VC4_V3D_GRAPH_MAX_SOURCES];
    ULONG Flags;
    ULONG VertexCount;
    ULONG ClearColor;
    ULONG DestinationX;
    ULONG DestinationY;
    ULONG MinimumX;
    ULONG MinimumY;
    ULONG MaximumX;
    ULONG MaximumY;
    ULONG Reserved;
    ULONG ShaderUniforms[RPI5VC4_V3D_TERRAIN_UNIFORM_WORDS];
    RPI5VC4_V3D_VERTEX Vertices[RPI5VC4_V3D_GRAPH_MAX_VERTICES];
} RPI5VC4_V3D_GRAPH_PASS, *PRPI5VC4_V3D_GRAPH_PASS;

typedef struct _RPI5VC4_V3D_RENDER_GRAPH_REQUEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG CacheId;
    ULONG Flags;
    ULONG ResourceCount;
    ULONG PassCount;
    ULONG ReadbackResource;
    ULONG TerrainVertexCount;
    ULONG TerrainVertexBytes;
    ULONG TerrainVertexOffset;
    ULONG Reserved;
    RPI5VC4_V3D_GRAPH_RESOURCE
        Resources[RPI5VC4_V3D_GRAPH_MAX_RESOURCES];
    RPI5VC4_V3D_GRAPH_PASS Passes[RPI5VC4_V3D_GRAPH_MAX_PASSES];
    UCHAR Pixels[1];
} RPI5VC4_V3D_RENDER_GRAPH_REQUEST,
  *PRPI5VC4_V3D_RENDER_GRAPH_REQUEST;

typedef struct _RPI5VC4_V3D_RENDER_GRAPH_RESULT
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Status;
    ULONG Flags;
    ULONG CacheId;
    ULONG PassCount;
    ULONG FailedPass;
    RPI5VC4_V3D_SELFTEST Diagnostics;
} RPI5VC4_V3D_RENDER_GRAPH_RESULT,
  *PRPI5VC4_V3D_RENDER_GRAPH_RESULT;

typedef struct _RPI5VC4_V3D_READ_GRAPH_REQUEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG CacheId;
    ULONG Resource;
} RPI5VC4_V3D_READ_GRAPH_REQUEST,
  *PRPI5VC4_V3D_READ_GRAPH_REQUEST;

typedef struct _RPI5VC4_V3D_READ_GRAPH_RESULT
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Status;
    ULONG Flags;
    ULONG CacheId;
    ULONG Resource;
    ULONG Width;
    ULONG Height;
    ULONG Stride;
    ULONG PixelBytes;
    ULONG Pixels[1];
} RPI5VC4_V3D_READ_GRAPH_RESULT,
  *PRPI5VC4_V3D_READ_GRAPH_RESULT;

/*
 * The display driver sends only a bounded native 32-bpp GDI rectangle.  The
 * miniport converts it to its private tiled texture and builds the fixed V3D
 * copy job; no GPU address or command-list bytes cross this interface.
 */
#define RPI5VC4_GDI_FORMAT_BGRX8 1u

typedef struct _RPI5VC4_GDI_PRESENT_REQUEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Width;
    ULONG Height;
    ULONG DestinationX;
    ULONG DestinationY;
    ULONG Format;
    ULONG RowStride;
    ULONG PixelBytes;
    UCHAR Pixels[1];
} RPI5VC4_GDI_PRESENT_REQUEST, *PRPI5VC4_GDI_PRESENT_REQUEST;

typedef struct _RPI5VC4_GDI_PRESENT_RESULT
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Status;
    ULONG Flags;
    ULONG Width;
    ULONG Height;
    ULONG DestinationX;
    ULONG DestinationY;
    RPI5VC4_V3D_SELFTEST Diagnostics;
} RPI5VC4_GDI_PRESENT_RESULT, *PRPI5VC4_GDI_PRESENT_RESULT;

#define RPI5VC4_OGL_STATS_VERSION 3

typedef struct _RPI5VC4_OGL_STATS
{
    ULONG Size;
    ULONG Version;
    ULONG HardwareClearCount;
    ULONG SoftwareClearCount;
    ULONG HardwareClearFailureCount;
    ULONG LastHardwareStatus;
    ULONG LastWidth;
    ULONG LastHeight;
    ULONG HardwareTriangleCount;
    ULONG SoftwareTriangleCount;
    ULONG HardwareTriangleFailureCount;
    ULONG LastTriangleStatus;
    ULONG LastTriangleWidth;
    ULONG LastTriangleHeight;
    ULONG LastTriangleCoveredPixels;
    ULONG HardwareProgramTriangleCount;
    ULONG LastProgramTriangleName;
} RPI5VC4_OGL_STATS, *PRPI5VC4_OGL_STATS;

#endif /* _REACTOS_RPI5VC4_XPDM_H_ */
