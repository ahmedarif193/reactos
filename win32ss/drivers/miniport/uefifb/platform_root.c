/*
 * PROJECT:     ReactOS UEFI GOP Framebuffer Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Firmware-root binding and headless fallback provider.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "uefifb.h"

#define UEFIFB_HEADLESS_WIDTH  1024
#define UEFIFB_HEADLESS_HEIGHT 768
#define UEFIFB_HEADLESS_BPP    32

static
VP_STATUS
NTAPI
UefiFbRootValidateFrameBuffer(
    _In_ PVOID HwDeviceExtension,
    _In_ const LOADER_PARAMETER_FRAMEBUFFER *FrameBuffer)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(FrameBuffer);
    return NO_ERROR;
}

/*
 * Keep a usable display surface when firmware did not publish GOP state.
 * This is deliberately a root-provider policy; PCI-bound providers must not
 * claim an off-screen allocation as scanout memory owned by their adapter.
 */
static
VP_STATUS
NTAPI
UefiFbRootInitializeFallback(
    _Inout_ PUEFIFB_DEVICE_EXTENSION DeviceExtension)
{
    const ULONG Width = UEFIFB_HEADLESS_WIDTH;
    const ULONG Height = UEFIFB_HEADLESS_HEIGHT;
    const ULONG BytesPerPixel = UEFIFB_HEADLESS_BPP / 8;
    const ULONG Pitch = Width * BytesPerPixel;
    const SIZE_T SizeInBytes = (SIZE_T)Pitch * Height;
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Boundary;
    PVOID Buffer;

    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)-1;
    Boundary.QuadPart = 0;

    /*
     * Match the write-combined mapping requested later through videoport.
     * A cached kernel alias would use conflicting attributes on ARM64.
     */
    Buffer = MmAllocateContiguousMemorySpecifyCache(SizeInBytes,
                                                    Low,
                                                    High,
                                                    Boundary,
                                                    MmWriteCombined);
    if (Buffer == NULL)
    {
        VideoPortDebugPrint(Error,
            "UefiFb: headless framebuffer alloc (%lu bytes) failed\n",
            (ULONG)SizeInBytes);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    VideoPortZeroMemory(Buffer, (ULONG)SizeInBytes);

    DeviceExtension->HeadlessBuffer = Buffer;
    DeviceExtension->FrameBufferPhysical = MmGetPhysicalAddress(Buffer);
    DeviceExtension->FrameBufferSize = (ULONG)SizeInBytes;
    DeviceExtension->ScreenWidth = Width;
    DeviceExtension->ScreenHeight = Height;
    DeviceExtension->PixelsPerScanLine = Width;
    DeviceExtension->BitsPerPixel = UEFIFB_HEADLESS_BPP;
    DeviceExtension->BytesPerScanLine = Pitch;
    DeviceExtension->RedMask = 0x00FF0000;
    DeviceExtension->GreenMask = 0x0000FF00;
    DeviceExtension->BlueMask = 0x000000FF;
    DeviceExtension->ReservedMask = 0xFF000000;
    DeviceExtension->MappedFrameBuffer = NULL;
    DeviceExtension->CurrentMode = 0;

    VideoPortDebugPrint(Info,
        "UefiFb: no usable firmware framebuffer; created %lux%lu "
        "headless RAM surface at 0x%I64x\n",
        Width,
        Height,
        DeviceExtension->FrameBufferPhysical.QuadPart);

    return NO_ERROR;
}

const UEFIFB_PLATFORM_INTERFACE UefiFbPlatform =
{
    Internal,
    NULL,
    UefiFbRootValidateFrameBuffer,
    UefiFbRootInitializeFallback
};
