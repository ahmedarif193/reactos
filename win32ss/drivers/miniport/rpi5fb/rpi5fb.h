/*
 * PROJECT:     ReactOS Raspberry Pi 5 framebuffer miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared declarations for the Raspberry Pi 5 framebuffer miniport.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _RPI5FB_PCH_
#define _RPI5FB_PCH_

#include <ntddk.h>
#include <dderror.h>
#define __BROKEN__
#include <miniport.h>
#undef __BROKEN__
#include <video.h>
#include <devioctl.h>

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
} LOADER_PARAMETER_FRAMEBUFFER, *PLOADER_PARAMETER_FRAMEBUFFER;

typedef struct _RPI5FB_DEVICE_EXTENSION
{
    PHYSICAL_ADDRESS FrameBufferPhysical;
    ULONG FrameBufferSize;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    ULONG PixelsPerScanLine;
    ULONG BitsPerPixel;
    ULONG BytesPerScanLine;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    VIDEO_MODE_INFORMATION ModeInfo;
    PVOID MappedFrameBuffer;
    ULONG CurrentMode;
} RPI5FB_DEVICE_EXTENSION, *PRPI5FB_DEVICE_EXTENSION;

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

VP_STATUS
NTAPI
Rpi5FbFindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again);

BOOLEAN
NTAPI
Rpi5FbInitialize(
    _In_ PVOID HwDeviceExtension);

BOOLEAN
NTAPI
Rpi5FbStartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket);

BOOLEAN
NTAPI
Rpi5FbResetHw(
    _In_ PVOID DeviceExtension,
    _In_ ULONG Columns,
    _In_ ULONG Rows);

VP_STATUS
NTAPI
Rpi5FbGetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS
NTAPI
Rpi5FbSetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

#endif /* _RPI5FB_PCH_ */
