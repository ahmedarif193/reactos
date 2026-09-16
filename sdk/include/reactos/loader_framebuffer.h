/*
 * PROJECT:     ReactOS Loader/Display Handoff
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared firmware framebuffer handoff ABI.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

typedef struct _LOADER_PARAMETER_FRAMEBUFFER
{
    LARGE_INTEGER FrameBufferBase;
    ULONG FrameBufferSize;
    ULONG HorizontalResolution;
    ULONG VerticalResolution;
    ULONG PixelsPerScanLine;
    ULONG PixelFormat;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG Reserved;
    ULONG Dpi;
} LOADER_PARAMETER_FRAMEBUFFER, *PLOADER_PARAMETER_FRAMEBUFFER;

typedef enum _LOADER_PARAMETER_FRAMEBUFFER_ROTATION
{
    LoaderFramebufferRotationIdentity = 0,
    LoaderFramebufferRotation90,
    LoaderFramebufferRotation180,
    LoaderFramebufferRotation270
} LOADER_PARAMETER_FRAMEBUFFER_ROTATION;

#define LOADER_PARAMETER_FRAMEBUFFER_TRANSFORM_VERSION 1
#define LOADER_PARAMETER_FRAMEBUFFER_FLAG_FIXED_SCANOUT 0x00000001

/*
 * Optional description of the logical display presented by a transformed
 * firmware framebuffer.  LOADER_PARAMETER_FRAMEBUFFER always describes the
 * real linear memory layout; this structure describes how logical display
 * coordinates map onto it.
 */
typedef struct _LOADER_PARAMETER_FRAMEBUFFER_TRANSFORM
{
    ULONG Size;
    ULONG Version;
    ULONG Flags;
    ULONG Rotation;
    ULONG LogicalWidth;
    ULONG LogicalHeight;
} LOADER_PARAMETER_FRAMEBUFFER_TRANSFORM,
 *PLOADER_PARAMETER_FRAMEBUFFER_TRANSFORM;

#define LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT 96
#define LOADER_PARAMETER_FRAMEBUFFER_DPI_MIN     96
#define LOADER_PARAMETER_FRAMEBUFFER_DPI_MAX     480
