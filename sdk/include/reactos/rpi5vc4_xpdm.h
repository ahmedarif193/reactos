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

#define RPI5VC4_ESCAPE_QUERY_V3D 0x52505633 /* "RPV3" */
#define RPI5VC4_ESCAPE_RUN_V3D_SELFTEST 0x52505654 /* "RPVT" */
#define RPI5VC4_ESCAPE_RENDER_CLEAR 0x5250434C /* "RPCL" */
#define RPI5VC4_ESCAPE_RENDER_TRIANGLE 0x52505452 /* "RPTR" */

#define RPI5VC4_XPDM_ABI_VERSION 4

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

#define RPI5VC4_V3D_CLEAR_MAX_WIDTH  512u
#define RPI5VC4_V3D_CLEAR_MAX_HEIGHT 512u

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
 * A triangle request carries only three clip-space positions and their RGBA
 * colors. The miniport supplies fixed V3D 7.1 shaders and builds every state,
 * attribute, binning, and rendering packet. IEEE-754 values are represented
 * as raw words so the kernel never enters a floating-point context.
 */
typedef struct _RPI5VC4_V3D_VERTEX
{
    ULONG Position[4];
    ULONG Color[4];
} RPI5VC4_V3D_VERTEX, *PRPI5VC4_V3D_VERTEX;

typedef struct _RPI5VC4_V3D_TRIANGLE_REQUEST
{
    ULONG Size;
    ULONG AbiVersion;
    ULONG Width;
    ULONG Height;
    ULONG ClearColor;
    ULONG Reserved[3];
    RPI5VC4_V3D_VERTEX Vertices[3];
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
    RPI5VC4_V3D_SELFTEST Diagnostics;
    ULONG Pixels[1];
} RPI5VC4_V3D_TRIANGLE_RESULT, *PRPI5VC4_V3D_TRIANGLE_RESULT;

#define RPI5VC4_OGL_STATS_VERSION 1

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
} RPI5VC4_OGL_STATS, *PRPI5VC4_OGL_STATS;

#endif /* _REACTOS_RPI5VC4_XPDM_H_ */
