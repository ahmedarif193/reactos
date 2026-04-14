/*
 * ReactOS UEFI Framebuffer Video Miniport Driver
 *
 * Copyright (C) 2026 ReactOS Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _UEFIFB_PCH_
#define _UEFIFB_PCH_

/* INCLUDES *******************************************************************/

#include <ntdef.h>
#include <dderror.h>
#include <miniport.h>
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

/* DEVICE EXTENSION ***********************************************************/

typedef struct _UEFIFB_DEVICE_EXTENSION
{
    /* Framebuffer parameters (from kernel GOP handoff) */
    PHYSICAL_ADDRESS    FrameBufferPhysical;    /* Physical base address */
    ULONG               FrameBufferSize;        /* Total size in bytes */
    ULONG               ScreenWidth;            /* Horizontal pixels */
    ULONG               ScreenHeight;           /* Vertical pixels */
    ULONG               PixelsPerScanLine;      /* Pitch in pixels */
    ULONG               BitsPerPixel;           /* Color depth */
    ULONG               BytesPerScanLine;       /* Pitch in bytes */
    ULONG               RedMask;
    ULONG               GreenMask;
    ULONG               BlueMask;

    /* Runtime state */
    VIDEO_MODE_INFORMATION ModeInfo;            /* Single mode descriptor */
    PVOID               MappedFrameBuffer;      /* Current kernel VA mapping */
    ULONG               CurrentMode;            /* Active mode index (always 0) */

} UEFIFB_DEVICE_EXTENSION, *PUEFIFB_DEVICE_EXTENSION;

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

/* FUNCTION PROTOTYPES ********************************************************/

VP_STATUS NTAPI
UefiFbFindAdapter(
    IN PVOID HwDeviceExtension,
    IN PVOID HwContext,
    IN PWSTR ArgumentString,
    IN OUT PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    OUT PUCHAR Again);

BOOLEAN NTAPI
UefiFbInitialize(PVOID HwDeviceExtension);

BOOLEAN NTAPI
UefiFbStartIO(
    PVOID HwDeviceExtension,
    PVIDEO_REQUEST_PACKET RequestPacket);

BOOLEAN NTAPI
UefiFbResetHw(
    PVOID DeviceExtension,
    ULONG Columns,
    ULONG Rows);

VP_STATUS NTAPI
UefiFbGetPowerState(
    PVOID HwDeviceExtension,
    ULONG HwId,
    PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS NTAPI
UefiFbSetPowerState(
    PVOID HwDeviceExtension,
    ULONG HwId,
    PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS NTAPI
UefiFbGetVideoChildDescriptor(
    IN PVOID HwDeviceExtension,
    IN PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
    OUT PVIDEO_CHILD_TYPE VideoChildType,
    OUT PUCHAR pChildDescriptor,
    OUT PULONG UId,
    OUT PULONG pUnused);

#endif /* _UEFIFB_PCH_ */
