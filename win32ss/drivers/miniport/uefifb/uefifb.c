/*
 * PROJECT:     ReactOS UEFI GOP Framebuffer Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Videoport miniport that exposes the firmware-provided GOP
 *              linear framebuffer (preserved by ntoskrnl across the loader
 *              handoff) to the framebuf.dll display driver.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Device binding, firmware-resource ownership, and headless fallback policy
 * are supplied by the platform provider linked into each miniport target.
 */

#include "uefifb.h"

/* PUBLIC AND PRIVATE FUNCTIONS ***********************************************/

ULONG
NTAPI
DriverEntry(
    _In_ PVOID Context1,
    _In_ PVOID Context2)
{
    VIDEO_HW_INITIALIZATION_DATA InitData;

    VideoPortZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
    InitData.StartingDeviceNumber = 0;
    InitData.AdapterInterfaceType = UefiFbPlatform.AdapterInterfaceType;
    InitData.HwFindAdapter = UefiFbFindAdapter;
    InitData.HwInitialize = UefiFbInitialize;
    InitData.HwStartIO = UefiFbStartIO;
    InitData.HwResetHw = UefiFbResetHw;
    InitData.HwGetPowerState = UefiFbGetPowerState;
    InitData.HwSetPowerState = UefiFbSetPowerState;
    InitData.HwGetVideoChildDescriptor =
        UefiFbPlatform.GetVideoChildDescriptor;
    InitData.HwDeviceExtensionSize = sizeof(UEFIFB_DEVICE_EXTENSION);

    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}

static BOOLEAN
UefiFbDecodePixelFormat(
    _In_ const LOADER_PARAMETER_FRAMEBUFFER *FrameBuffer,
    _Out_ PULONG BitsPerPixel,
    _Out_ PULONG BytesPerPixel,
    _Out_ PULONG RedMask,
    _Out_ PULONG GreenMask,
    _Out_ PULONG BlueMask,
    _Out_ PULONG ReservedMask)
{
    ULONG ColorMasks;
    ULONG AllMasks;
    ULONG ValidBits;

    *RedMask = FrameBuffer->RedMask;
    *GreenMask = FrameBuffer->GreenMask;
    *BlueMask = FrameBuffer->BlueMask;
    *ReservedMask = FrameBuffer->Reserved;

    /*
     * PixelFormat in LOADER_PARAMETER_FRAMEBUFFER is the loader-normalized
     * bits-per-pixel value. FreeLdr has already converted the EFI GOP enum
     * into this value and copied the corresponding channel masks.
     */
    switch (FrameBuffer->PixelFormat)
    {
        case 16:
        case 24:
        case 32:
            *BitsPerPixel = FrameBuffer->PixelFormat;
            *BytesPerPixel = (*BitsPerPixel + 7) / 8;
            break;

        default:
            return FALSE;
    }

    ColorMasks = *RedMask | *GreenMask | *BlueMask;
    AllMasks = ColorMasks | *ReservedMask;
    ValidBits = (*BitsPerPixel == 32) ?
                    MAXULONG :
                    (1UL << *BitsPerPixel) - 1;

    return ColorMasks != 0 &&
           (AllMasks & ~ValidBits) == 0 &&
           (*RedMask & *GreenMask) == 0 &&
           (*RedMask & *BlueMask) == 0 &&
           (*GreenMask & *BlueMask) == 0 &&
           (*ReservedMask & ColorMasks) == 0;
}

static ULONG
UefiFbMaskBitCount(
    _In_ ULONG Mask)
{
    ULONG Count = 0;

    while (Mask != 0)
    {
        Count += Mask & 1;
        Mask >>= 1;
    }

    return Count;
}

VP_STATUS
NTAPI
UefiFbFindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again)
{
    PUEFIFB_DEVICE_EXTENSION DeviceExtension =
        (PUEFIFB_DEVICE_EXTENSION)HwDeviceExtension;
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    VP_STATUS Status;
    ULONG BytesPerPixel;
    ULONGLONG VisibleFrameBufferSize;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG ReservedMask;
    ULONG BitsPerPixel;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;

    VideoPortZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    if (ConfigInfo->Length < sizeof(VIDEO_PORT_CONFIG_INFO))
        return ERROR_INVALID_PARAMETER;

    /*
     * Prefer the real firmware framebuffer when the loader handed one off.
     * When it did not - a headless boot where no display was attached at
     * power-on, so the firmware never allocated a GOP framebuffer - fall back
     * to an off-screen RAM surface so a display device still exists. Declining
     * here instead leaves the machine with zero display adapters, which
     * bugchecks win32k with VIDEO_DRIVER_INIT_FAILURE (0xB4).
     */
    if (!InbvGetGopFrameBufferInfo(&FbInfo) ||
        FbInfo.FrameBufferBase.QuadPart == 0 ||
        FbInfo.FrameBufferSize == 0 ||
        FbInfo.HorizontalResolution == 0 ||
        FbInfo.VerticalResolution == 0 ||
        FbInfo.PixelsPerScanLine < FbInfo.HorizontalResolution ||
        FbInfo.PixelFormat == 0)
    {
        Status = UefiFbPlatform.InitializeFallback(DeviceExtension);
        if (Status != NO_ERROR)
            return Status;
        goto Configured;
    }

    if (!UefiFbDecodePixelFormat(&FbInfo,
                                 &BitsPerPixel,
                                 &BytesPerPixel,
                                 &RedMask,
                                 &GreenMask,
                                 &BlueMask,
                                 &ReservedMask))
    {
        Status = UefiFbPlatform.InitializeFallback(DeviceExtension);
        if (Status != NO_ERROR)
            return Status;
        goto Configured;
    }

    if (BytesPerPixel == 0 ||
        FbInfo.PixelsPerScanLine > MAXULONG / BytesPerPixel)
    {
        Status = UefiFbPlatform.InitializeFallback(DeviceExtension);
        if (Status != NO_ERROR)
            return Status;
        goto Configured;
    }

    DeviceExtension->BytesPerScanLine =
        FbInfo.PixelsPerScanLine * BytesPerPixel;
    if (FbInfo.VerticalResolution >
        (~(ULONGLONG)0) / DeviceExtension->BytesPerScanLine)
    {
        Status = UefiFbPlatform.InitializeFallback(DeviceExtension);
        if (Status != NO_ERROR)
            return Status;
        goto Configured;
    }

    VisibleFrameBufferSize =
        (ULONGLONG)FbInfo.VerticalResolution *
        DeviceExtension->BytesPerScanLine;
    if (VisibleFrameBufferSize == 0 ||
        VisibleFrameBufferSize > FbInfo.FrameBufferSize ||
        VisibleFrameBufferSize > MAXULONG)
    {
        VideoPortDebugPrint(Error,
            "UefiFb: visible size 0x%I64x exceeds aperture 0x%lx, "
            "rejecting firmware mode\n",
            VisibleFrameBufferSize,
            FbInfo.FrameBufferSize);
        Status = UefiFbPlatform.InitializeFallback(DeviceExtension);
        if (Status != NO_ERROR)
            return Status;
        goto Configured;
    }

    Status = UefiFbPlatform.ValidateFrameBuffer(DeviceExtension, &FbInfo);
    if (Status != NO_ERROR)
        return Status;

    DeviceExtension->FrameBufferPhysical = FbInfo.FrameBufferBase;
    DeviceExtension->FrameBufferSize = (ULONG)VisibleFrameBufferSize;
    DeviceExtension->ScreenWidth = FbInfo.HorizontalResolution;
    DeviceExtension->ScreenHeight = FbInfo.VerticalResolution;
    DeviceExtension->PixelsPerScanLine = FbInfo.PixelsPerScanLine;
    DeviceExtension->BitsPerPixel = BitsPerPixel;
    DeviceExtension->RedMask = RedMask;
    DeviceExtension->GreenMask = GreenMask;
    DeviceExtension->BlueMask = BlueMask;
    DeviceExtension->ReservedMask = ReservedMask;
    DeviceExtension->MappedFrameBuffer = NULL;
    DeviceExtension->CurrentMode = 0;

    VideoPortDebugPrint(Info,
        "UefiFb: %lux%lu@%lubpp at 0x%I64x (%lu bytes visible)\n",
        DeviceExtension->ScreenWidth,
        DeviceExtension->ScreenHeight,
        DeviceExtension->BitsPerPixel,
        DeviceExtension->FrameBufferPhysical.QuadPart,
        DeviceExtension->FrameBufferSize);

Configured:
    ConfigInfo->NumEmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntriesContext = 0;
    ConfigInfo->HardwareStateSize = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress = DeviceExtension->FrameBufferPhysical;
    ConfigInfo->VdmPhysicalVideoMemoryLength = DeviceExtension->FrameBufferSize;

    return NO_ERROR;
}

BOOLEAN
NTAPI
UefiFbInitialize(
    _In_ PVOID HwDeviceExtension)
{
    PUEFIFB_DEVICE_EXTENSION DeviceExtension =
        (PUEFIFB_DEVICE_EXTENSION)HwDeviceExtension;
    PVIDEO_MODE_INFORMATION Mode = &DeviceExtension->ModeInfo;
    ULONG BytesPerPixel = (DeviceExtension->BitsPerPixel + 7) / 8;
    const ULONG Dpi = 96;

    VideoPortZeroMemory(Mode, sizeof(*Mode));

    Mode->Length = sizeof(*Mode);
    Mode->ModeIndex = 0;
    Mode->VisScreenWidth = DeviceExtension->ScreenWidth;
    Mode->VisScreenHeight = DeviceExtension->ScreenHeight;
    Mode->ScreenStride = DeviceExtension->PixelsPerScanLine * BytesPerPixel;
    Mode->NumberOfPlanes = 1;
    Mode->BitsPerPlane = DeviceExtension->BitsPerPixel;
    Mode->Frequency = 60;

    /* Physical size derived from a 96-DPI assumption (rounded) */
    Mode->XMillimeter =
        ((ULONGLONG)DeviceExtension->ScreenWidth * 254 + (Dpi * 5)) / (Dpi * 10);
    Mode->YMillimeter =
        ((ULONGLONG)DeviceExtension->ScreenHeight * 254 + (Dpi * 5)) / (Dpi * 10);

    Mode->NumberRedBits = UefiFbMaskBitCount(DeviceExtension->RedMask);
    Mode->NumberGreenBits = UefiFbMaskBitCount(DeviceExtension->GreenMask);
    Mode->NumberBlueBits = UefiFbMaskBitCount(DeviceExtension->BlueMask);
    Mode->RedMask = DeviceExtension->RedMask;
    Mode->GreenMask = DeviceExtension->GreenMask;
    Mode->BlueMask = DeviceExtension->BlueMask;
    Mode->DriverSpecificAttributeFlags = DeviceExtension->ReservedMask;

    Mode->AttributeFlags = VIDEO_MODE_GRAPHICS |
                           VIDEO_MODE_COLOR |
                           VIDEO_MODE_NO_OFF_SCREEN;

    Mode->VideoMemoryBitmapWidth = DeviceExtension->ScreenWidth;
    Mode->VideoMemoryBitmapHeight = DeviceExtension->ScreenHeight;

    return TRUE;
}

BOOLEAN
NTAPI
UefiFbStartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket)
{
    PUEFIFB_DEVICE_EXTENSION DeviceExtension =
        (PUEFIFB_DEVICE_EXTENSION)HwDeviceExtension;
    VP_STATUS Status = ERROR_INVALID_FUNCTION;
    PVIDEO_MODE_INFORMATION ModeInfo;
    PVIDEO_MEMORY VideoMemory;
    PVIDEO_MEMORY_INFORMATION MemoryInfo;
    PVIDEO_MODE VideoMode;
    PVIDEO_NUM_MODES NumModes;
    PVIDEO_POINTER_CAPABILITIES PointerCaps;
    PHYSICAL_ADDRESS FrameBuffer;
    ULONG inIoSpace;

    switch (RequestPacket->IoControlCode)
    {
        case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES:
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_NUM_MODES))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            NumModes = (PVIDEO_NUM_MODES)RequestPacket->OutputBuffer;
            NumModes->NumModes = 1;
            NumModes->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);
            RequestPacket->StatusBlock->Information = sizeof(VIDEO_NUM_MODES);
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_QUERY_AVAIL_MODES:
        case IOCTL_VIDEO_QUERY_CURRENT_MODE:
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            ModeInfo = (PVIDEO_MODE_INFORMATION)RequestPacket->OutputBuffer;
            VideoPortMoveMemory(ModeInfo, &DeviceExtension->ModeInfo,
                                sizeof(VIDEO_MODE_INFORMATION));
            RequestPacket->StatusBlock->Information = sizeof(VIDEO_MODE_INFORMATION);
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_SET_CURRENT_MODE:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MODE))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            VideoMode = (PVIDEO_MODE)RequestPacket->InputBuffer;
            if (VideoMode->RequestedMode != 0)
            {
                Status = ERROR_INVALID_PARAMETER;
                break;
            }
            DeviceExtension->CurrentMode = 0;
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_RESET_DEVICE:
            /* No text-mode to return to on a firmware framebuffer */
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_MAP_VIDEO_MEMORY:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY) ||
                RequestPacket->OutputBufferLength < sizeof(VIDEO_MEMORY_INFORMATION))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            VideoMemory = (PVIDEO_MEMORY)RequestPacket->InputBuffer;
            MemoryInfo = (PVIDEO_MEMORY_INFORMATION)RequestPacket->OutputBuffer;
            FrameBuffer = DeviceExtension->FrameBufferPhysical;
            inIoSpace = VIDEO_MEMORY_SPACE_MEMORY | VIDEO_MEMORY_SPACE_P6CACHE;
            MemoryInfo->VideoRamBase = VideoMemory->RequestedVirtualAddress;
            MemoryInfo->VideoRamLength = DeviceExtension->FrameBufferSize;
            Status = VideoPortMapMemory(DeviceExtension,
                                        FrameBuffer,
                                        &MemoryInfo->VideoRamLength,
                                        &inIoSpace,
                                        &MemoryInfo->VideoRamBase);
            if (Status == NO_ERROR)
            {
                MemoryInfo->FrameBufferBase = MemoryInfo->VideoRamBase;
                MemoryInfo->FrameBufferLength = MemoryInfo->VideoRamLength;
                DeviceExtension->MappedFrameBuffer = MemoryInfo->VideoRamBase;
                RequestPacket->StatusBlock->Information =
                    sizeof(VIDEO_MEMORY_INFORMATION);
            }
            else
            {
                VideoPortDebugPrint(Error,
                    "UefiFb: VideoPortMapMemory failed (0x%lx)\n", Status);
            }
            break;

        case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            VideoMemory = (PVIDEO_MEMORY)RequestPacket->InputBuffer;
            Status = VideoPortUnmapMemory(DeviceExtension,
                                          VideoMemory->RequestedVirtualAddress,
                                          NULL);
            DeviceExtension->MappedFrameBuffer = NULL;
            break;

        case IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES:
            if (RequestPacket->OutputBufferLength <
                    sizeof(VIDEO_POINTER_CAPABILITIES))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            PointerCaps =
                (PVIDEO_POINTER_CAPABILITIES)RequestPacket->OutputBuffer;
            VideoPortZeroMemory(PointerCaps, sizeof(*PointerCaps));
            /* No hardware cursor - GDI falls back to software */
            RequestPacket->StatusBlock->Information =
                sizeof(VIDEO_POINTER_CAPABILITIES);
            Status = NO_ERROR;
            break;

        default:
            /* Remaining IOCTLs (palette, pointer, passthrough VDM ranges) are
             * intentionally unsupported; framebuf.dll handles the rejection. */
            Status = ERROR_INVALID_FUNCTION;
            break;
    }

    RequestPacket->StatusBlock->Status = Status;
    return TRUE;
}

BOOLEAN
NTAPI
UefiFbResetHw(
    _In_ PVOID DeviceExtension,
    _In_ ULONG Columns,
    _In_ ULONG Rows)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(Columns);
    UNREFERENCED_PARAMETER(Rows);
    /* The firmware framebuffer has no text-mode to reset to. Returning
     * FALSE delegates the fallback (e.g. INT 10h) to the HAL, which on
     * UEFI systems is also a no-op. */
    return FALSE;
}

VP_STATUS
NTAPI
UefiFbGetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);

    if (VideoPowerControl == NULL)
        return ERROR_INVALID_PARAMETER;

    /* Firmware framebuffer has no DPMS - always reports ON. */
    VideoPowerControl->PowerState = VideoPowerOn;
    return NO_ERROR;
}

VP_STATUS
NTAPI
UefiFbSetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);

    if (VideoPowerControl == NULL)
        return ERROR_INVALID_PARAMETER;

    /* Accept VideoPowerOn / S0 transitions; reject DPMS power-down because
     * there is no way to turn the firmware framebuffer back on. */
    if (VideoPowerControl->PowerState == VideoPowerOn)
        return NO_ERROR;

    return ERROR_INVALID_FUNCTION;
}
