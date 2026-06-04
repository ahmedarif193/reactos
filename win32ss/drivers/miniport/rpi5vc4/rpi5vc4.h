/*
 * PROJECT:     ReactOS Raspberry Pi 5 framebuffer miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared declarations for the Raspberry Pi 5 framebuffer miniport.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _RPI5VC4_PCH_
#define _RPI5VC4_PCH_

#include <ntddk.h>
#include <dderror.h>
#define __BROKEN__
#include <miniport.h>
#undef __BROKEN__
#include <video.h>
#include <devioctl.h>

#define RPI5VC4_CURSOR_WIDTH 64
#define RPI5VC4_CURSOR_HEIGHT 64

#define IOCTL_VIDEO_RPI5VC4_LATCH_SCANOUT \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x830, METHOD_BUFFERED, FILE_ANY_ACCESS)

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

typedef struct _RPI5VC4_DEVICE_EXTENSION
{
    PHYSICAL_ADDRESS FirmwareFrameBufferPhysical;
    PHYSICAL_ADDRESS FrameBufferPhysical;
    PVOID FrameBufferVirtual;
    ULONG FrameBufferSize;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    ULONG PixelsPerScanLine;
    ULONG BitsPerPixel;
    ULONG BytesPerScanLine;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG ReservedMask;
    VIDEO_MODE_INFORMATION ModeInfo;
    PVOID MappedFrameBuffer;
    ULONG CurrentMode;

    BOOLEAN PixelValveValid;
    ULONG PixelValveIndex;
    PHYSICAL_ADDRESS PixelValvePhysical;
    ULONG PixelValveControl;
    ULONG PixelValveVControl;
    ULONG PixelValveVsyncEven;
    ULONG PixelValveHorzA;
    ULONG PixelValveHorzB;
    ULONG PixelValveVertA;
    ULONG PixelValveVertB;
    ULONG PixelValveHactAct;

    /* Hardware cursor composited by the HVS as a second display-list plane. */
    PVOID CursorVa;                   /* CPU mapping of the cursor surface      */
    PHYSICAL_ADDRESS CursorPhys;      /* physical base the HVS scans the cursor */
    ULONG CursorWidth;
    ULONG CursorHeight;
    LONG CursorX;                     /* top-left, screen coordinates          */
    LONG CursorY;
    BOOLEAN CursorVisible;
    BOOLEAN CursorShapeValid;
} RPI5VC4_DEVICE_EXTENSION, *PRPI5VC4_DEVICE_EXTENSION;

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

VP_STATUS
NTAPI
Rpi5Vc4FindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again);

BOOLEAN
NTAPI
Rpi5Vc4Initialize(
    _In_ PVOID HwDeviceExtension);

BOOLEAN
NTAPI
Rpi5Vc4StartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket);

BOOLEAN
NTAPI
Rpi5Vc4ResetHw(
    _In_ PVOID DeviceExtension,
    _In_ ULONG Columns,
    _In_ ULONG Rows);

VP_STATUS
NTAPI
Rpi5Vc4GetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS
NTAPI
Rpi5Vc4SetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

#endif /* _RPI5VC4_PCH_ */
