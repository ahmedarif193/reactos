/*
 * PROJECT:     ReactOS UEFI GOP Framebuffer Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared declarations for the UEFI framebuffer video miniport.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _UEFIFB_PCH_
#define _UEFIFB_PCH_

/* INCLUDES *******************************************************************/

#include <ntddk.h>
#include <dderror.h>
#define __BROKEN__
#include <miniport.h>
#undef __BROKEN__
#include <video.h>
#include <devioctl.h>

/*
 * Private copy of the loader framebuffer descriptor. Must be kept in sync
 * with sdk/include/reactos/arc/arc.h:_LOADER_PARAMETER_FRAMEBUFFER - we cannot
 * include arc.h directly from a videoport miniport because it depends on
 * PFN_NUMBER from mmtypes.h which the miniport headers do not pull in.
 */
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

/* DEVICE EXTENSION ***********************************************************/

typedef struct _UEFIFB_DEVICE_EXTENSION
{
    /* Firmware-provided framebuffer parameters (saved by ntoskrnl at phase 1) */
    PHYSICAL_ADDRESS FrameBufferPhysical;   /* Physical base address */
    ULONG FrameBufferSize;                  /* Visible size in bytes */
    ULONG ScreenWidth;                      /* Horizontal pixels */
    ULONG ScreenHeight;                     /* Vertical pixels */
    ULONG PixelsPerScanLine;                /* Pitch in pixels */
    ULONG BitsPerPixel;                     /* Color depth */
    ULONG BytesPerScanLine;                 /* Pitch in bytes */
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG ReservedMask;

    /* Runtime state */
    VIDEO_MODE_INFORMATION ModeInfo;        /* Single mode descriptor */
    PVOID MappedFrameBuffer;                /* Current user-mode mapping */
    ULONG CurrentMode;                      /* Active mode index (always 0) */

    /*
     * Headless fallback backing store. NULL when scanning out a real firmware
     * framebuffer; otherwise the kernel VA of the RAM surface synthesised when
     * the firmware exposed no GOP (no display attached at power-on).
     */
    PVOID HeadlessBuffer;
} UEFIFB_DEVICE_EXTENSION, *PUEFIFB_DEVICE_EXTENSION;

/* KERNEL-EXPORTED GOP ACCESSOR ***********************************************/

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

/* MINIPORT ENTRY POINTS ******************************************************/

VP_STATUS
NTAPI
UefiFbFindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again);

BOOLEAN
NTAPI
UefiFbInitialize(
    _In_ PVOID HwDeviceExtension);

BOOLEAN
NTAPI
UefiFbStartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket);

BOOLEAN
NTAPI
UefiFbResetHw(
    _In_ PVOID DeviceExtension,
    _In_ ULONG Columns,
    _In_ ULONG Rows);

VP_STATUS
NTAPI
UefiFbGetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS
NTAPI
UefiFbSetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

#endif /* _UEFIFB_PCH_ */
