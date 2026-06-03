/*
 * PROJECT:     ReactOS Intel Alder Lake-N display miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PCI-bound videoport miniport for Intel N100/Alder Lake-N GOP
 *              framebuffer handoff.
 */

#ifndef _INTELN100_PCH_
#define _INTELN100_PCH_

#include <ntdef.h>
#include <dderror.h>
#include <miniport.h>
#include <video.h>
#include <devioctl.h>

#define INTELN100_MAX_ACCESS_RANGES 8
#define INTELN100_TAG '01NI'

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

typedef struct _INTELN100_RANGE
{
    PHYSICAL_ADDRESS Base;
    ULONG Length;
    UCHAR InIoSpace;
} INTELN100_RANGE, *PINTELN100_RANGE;

typedef struct _INTELN100_DEVICE_EXTENSION
{
    INTELN100_RANGE MmioRange;
    INTELN100_RANGE GraphicsAperture;

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
} INTELN100_DEVICE_EXTENSION, *PINTELN100_DEVICE_EXTENSION;

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

VP_STATUS
NTAPI
IntelN100FindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again);

BOOLEAN
NTAPI
IntelN100Initialize(
    _In_ PVOID HwDeviceExtension);

BOOLEAN
NTAPI
IntelN100StartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket);

BOOLEAN
NTAPI
IntelN100ResetHw(
    _In_ PVOID DeviceExtension,
    _In_ ULONG Columns,
    _In_ ULONG Rows);

VP_STATUS
NTAPI
IntelN100GetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS
NTAPI
IntelN100SetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl);

VP_STATUS
NTAPI
IntelN100GetVideoChildDescriptor(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
    _Out_ PVIDEO_CHILD_TYPE VideoChildType,
    _Out_ PUCHAR ChildDescriptor,
    _Out_ PULONG UId,
    _Out_ PULONG Unused);

#endif /* _INTELN100_PCH_ */
