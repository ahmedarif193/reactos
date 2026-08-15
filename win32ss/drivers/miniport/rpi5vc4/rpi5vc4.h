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
#include <reactos/rpi5vc4_xpdm.h>

#define RPI5VC4_CURSOR_WIDTH 64
#define RPI5VC4_CURSOR_HEIGHT 64
#define RPI5VC4_ACPI_MEMORY_RESOURCE_COUNT 10

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
    VIDEO_ACCESS_RANGE V3dHubRange;
    VIDEO_ACCESS_RANGE V3dCoreRange;
    VIDEO_ACCESS_RANGE V3dSmsRange;
    VIDEO_ACCESS_RANGE HvsRange;
    VIDEO_ACCESS_RANGE HvsIommuRange;
    VIDEO_ACCESS_RANGE PixelValveRange[2];
    VIDEO_ACCESS_RANGE MopRange;
    VIDEO_ACCESS_RANGE MopletRange;
    VIDEO_ACCESS_RANGE DisplayInterruptRange;

    PHYSICAL_ADDRESS FirmwareFrameBufferPhysical;
    PHYSICAL_ADDRESS FrameBufferPhysical;
    PVOID FrameBufferVirtual;
    PVOID HeadlessBuffer;
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

    /* Cached MMIO mapping of the HVS register block (mapped once, reused). */
    PVOID HvsBase;

    /* Read-only V3D discovery state; submission remains disabled. */
    PVOID V3dSmsBase;
    PVOID V3dHubBase;
    PVOID V3dCoreBase;
    ULONG V3dFlags;
    ULONG V3dVersion;
    ULONG V3dCoreCount;
    ULONG V3dSmsReeCs;
    ULONG V3dSmsTeeCs;
    ULONG V3dHubIdent[4];
    ULONG V3dCoreIdent[3];
    ULONG V3dMmuDebugInfo;

    /* Cached MMIO mapping of the active PixelValve (mapped once, reused). */
    PVOID PixelValveBase;

    /* Validated cursor-plane location for the low-MMIO cursor move fast path. */
    ULONG HvsLptrsReg;
    ULONG HvsLptrsVal;
    ULONG HvsCursorHead;
    BOOLEAN HvsCursorFastValid;

    BOOLEAN PixelValveValid;
    ULONG PixelValveIndex;
    PHYSICAL_ADDRESS PixelValvePhysical;
    ULONG PixelValveLength;
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

VP_STATUS
NTAPI
Rpi5Vc4GetVideoChildDescriptor(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
    _Out_ PVIDEO_CHILD_TYPE VideoChildType,
    _Out_writes_bytes_(ChildEnumInfo->ChildDescriptorSize) PUCHAR ChildDescriptor,
    _Out_ PULONG UId,
    _Out_ PULONG Unused);

#endif /* _RPI5VC4_PCH_ */
