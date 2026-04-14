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
 *
 * DESCRIPTION:
 *   Video miniport driver for UEFI GOP framebuffers. Bridges the gap between
 *   the firmware-provided linear framebuffer (whose parameters are preserved
 *   by ntoskrnl after loader handoff) and the framebuf.dll GDI display driver.
 *
 *   This driver exposes a single video mode (the native resolution set by
 *   firmware) and handles the IOCTL contract expected by framebuf.dll.
 */

/* INCLUDES *******************************************************************/

#include "uefifb.h"

/* PUBLIC AND PRIVATE FUNCTIONS ***********************************************/

/*
 * DriverEntry
 *
 * Registers the miniport with the video port driver.
 */
ULONG NTAPI
DriverEntry(IN PVOID Context1, IN PVOID Context2)
{
    VIDEO_HW_INITIALIZATION_DATA InitData;
    PUNICODE_STRING RegistryPath = Context2;

    VideoPortDebugPrint(Error,
        "UefiFb: DriverEntry context1=%p context2=%p registry=%wZ\n",
        Context1,
        Context2,
        RegistryPath);

    VideoPortZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
    InitData.StartingDeviceNumber = 0;
    InitData.AdapterInterfaceType = Internal;
    InitData.HwFindAdapter = UefiFbFindAdapter;
    InitData.HwInitialize = UefiFbInitialize;
    InitData.HwStartIO = UefiFbStartIO;
    InitData.HwResetHw = UefiFbResetHw;
    InitData.HwGetPowerState = UefiFbGetPowerState;
    InitData.HwSetPowerState = UefiFbSetPowerState;
    /*
     * The GOP framebuffer is not exposed as a real PnP display adapter, so
     * videoprt must instantiate it through the legacy detection path.
     */
    InitData.HwGetVideoChildDescriptor = NULL;
    InitData.HwDeviceExtensionSize = sizeof(UEFIFB_DEVICE_EXTENSION);

    VideoPortDebugPrint(Error,
        "UefiFb: registering miniport iface=%lu power-callbacks=%u/%u child-callback=%u ext=%lu start=%lu\n",
        InitData.AdapterInterfaceType,
        InitData.HwGetPowerState != NULL,
        InitData.HwSetPowerState != NULL,
        InitData.HwGetVideoChildDescriptor != NULL,
        InitData.HwDeviceExtensionSize,
        InitData.StartingDeviceNumber);

    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}

/*
 * UefiFbFindAdapter
 *
 * Discovers the UEFI framebuffer by querying the GOP handoff that ntoskrnl
 * saved from the loader block during phase 1 initialization.
 */
VP_STATUS NTAPI
UefiFbFindAdapter(
    IN PVOID HwDeviceExtension,
    IN PVOID HwContext,
    IN PWSTR ArgumentString,
    IN OUT PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    OUT PUCHAR Again)
{
    PUEFIFB_DEVICE_EXTENSION DeviceExtension =
        (PUEFIFB_DEVICE_EXTENSION)HwDeviceExtension;
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG BytesPerPixel;
    ULONGLONG VisibleFrameBufferSize;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(ArgumentString);

    VideoPortDebugPrint(Error,
        "UefiFb: FindAdapter ext=%p ctx=%p args=%p config=%p len=%lu\n",
        HwDeviceExtension,
        HwContext,
        ArgumentString,
        ConfigInfo,
        ConfigInfo ? ConfigInfo->Length : 0);

    VideoPortZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    if (ConfigInfo->Length < sizeof(VIDEO_PORT_CONFIG_INFO))
    {
        VideoPortDebugPrint(Error,
            "UefiFb: FindAdapter got short VIDEO_PORT_CONFIG_INFO (%lu < %lu)\n",
            ConfigInfo->Length,
            sizeof(VIDEO_PORT_CONFIG_INFO));
        return ERROR_INVALID_PARAMETER;
    }

    if (!InbvGetGopFrameBufferInfo(&FbInfo))
    {
        VideoPortDebugPrint(Error,
            "UefiFb: GOP framebuffer information is unavailable\n");
        return ERROR_DEV_NOT_EXIST;
    }

    if (FbInfo.FrameBufferBase.QuadPart == 0 ||
        FbInfo.FrameBufferSize == 0 ||
        FbInfo.HorizontalResolution == 0 ||
        FbInfo.VerticalResolution == 0 ||
        FbInfo.PixelsPerScanLine == 0 ||
        FbInfo.PixelFormat == 0)
    {
        VideoPortDebugPrint(Error,
            "UefiFb: Invalid GOP framebuffer info (%lux%lu, pitch %lu, bpp %lu, size %lu)\n",
            FbInfo.HorizontalResolution,
            FbInfo.VerticalResolution,
            FbInfo.PixelsPerScanLine,
            FbInfo.PixelFormat,
            FbInfo.FrameBufferSize);
        return ERROR_DEV_NOT_EXIST;
    }

    BytesPerPixel = (FbInfo.PixelFormat + 7) / 8;
    VisibleFrameBufferSize =
        (ULONGLONG)FbInfo.VerticalResolution *
        FbInfo.PixelsPerScanLine *
        BytesPerPixel;
    if (VisibleFrameBufferSize == 0 ||
        VisibleFrameBufferSize > FbInfo.FrameBufferSize)
    {
        VideoPortDebugPrint(Error,
            "UefiFb: Visible framebuffer size 0x%I64x exceeds aperture 0x%lx\n",
            VisibleFrameBufferSize,
            FbInfo.FrameBufferSize);
        return ERROR_DEV_NOT_EXIST;
    }

    DeviceExtension->FrameBufferPhysical = FbInfo.FrameBufferBase;
    DeviceExtension->FrameBufferSize = (ULONG)VisibleFrameBufferSize;
    DeviceExtension->ScreenWidth = FbInfo.HorizontalResolution;
    DeviceExtension->ScreenHeight = FbInfo.VerticalResolution;
    DeviceExtension->PixelsPerScanLine = FbInfo.PixelsPerScanLine;
    DeviceExtension->BitsPerPixel = FbInfo.PixelFormat;
    DeviceExtension->RedMask = FbInfo.RedMask;
    DeviceExtension->GreenMask = FbInfo.GreenMask;
    DeviceExtension->BlueMask = FbInfo.BlueMask;

    /* Compute derived value */
    DeviceExtension->BytesPerScanLine =
        DeviceExtension->PixelsPerScanLine * BytesPerPixel;

    /* Validate essential parameters */
    if (DeviceExtension->ScreenWidth == 0 ||
        DeviceExtension->ScreenHeight == 0 ||
        DeviceExtension->BitsPerPixel == 0 ||
        DeviceExtension->FrameBufferSize == 0)
    {
        VideoPortDebugPrint(Error,
            "UefiFb: Invalid framebuffer parameters (%lux%lu, %lu bpp, %lu bytes)\n",
            DeviceExtension->ScreenWidth, DeviceExtension->ScreenHeight,
            DeviceExtension->BitsPerPixel, DeviceExtension->FrameBufferSize);
        return ERROR_DEV_NOT_EXIST;
    }

    VideoPortDebugPrint(Info,
        "UefiFb: Found UEFI framebuffer %lux%lu@%lubpp at 0x%lx%08lx (%lu bytes)\n",
        DeviceExtension->ScreenWidth,
        DeviceExtension->ScreenHeight,
        DeviceExtension->BitsPerPixel,
        DeviceExtension->FrameBufferPhysical.HighPart,
        DeviceExtension->FrameBufferPhysical.LowPart,
        DeviceExtension->FrameBufferSize);

    /* Initialize runtime state */
    DeviceExtension->MappedFrameBuffer = NULL;
    DeviceExtension->CurrentMode = 0;

    ConfigInfo->NumEmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntriesContext = 0;
    ConfigInfo->HardwareStateSize = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress = DeviceExtension->FrameBufferPhysical;
    ConfigInfo->VdmPhysicalVideoMemoryLength = DeviceExtension->FrameBufferSize;

    VideoPortDebugPrint(Error,
        "UefiFb: FindAdapter success fb=0x%lx%08lx visible=0x%lx stride=%lu config-iface=%lu bus=%lu\n",
        DeviceExtension->FrameBufferPhysical.HighPart,
        DeviceExtension->FrameBufferPhysical.LowPart,
        DeviceExtension->FrameBufferSize,
        DeviceExtension->BytesPerScanLine,
        ConfigInfo->AdapterInterfaceType,
        ConfigInfo->SystemIoBusNumber);

    *Again = FALSE;
    return NO_ERROR;
}

/*
 * UefiFbInitialize
 *
 * Full initialization after the HAL releases video control.
 * Builds a single VIDEO_MODE_INFORMATION entry from the discovered
 * framebuffer parameters.
 */
BOOLEAN NTAPI
UefiFbInitialize(PVOID HwDeviceExtension)
{
    PUEFIFB_DEVICE_EXTENSION DeviceExtension =
        (PUEFIFB_DEVICE_EXTENSION)HwDeviceExtension;
    PVIDEO_MODE_INFORMATION Mode = &DeviceExtension->ModeInfo;
    ULONG BytesPerPixel = (DeviceExtension->BitsPerPixel + 7) / 8;
    ULONG Dpi = 96;

    VideoPortZeroMemory(Mode, sizeof(VIDEO_MODE_INFORMATION));

    Mode->Length = sizeof(VIDEO_MODE_INFORMATION);
    Mode->ModeIndex = 0;
    Mode->VisScreenWidth = DeviceExtension->ScreenWidth;
    Mode->VisScreenHeight = DeviceExtension->ScreenHeight;
    Mode->ScreenStride = DeviceExtension->PixelsPerScanLine * BytesPerPixel;
    Mode->NumberOfPlanes = 1;
    Mode->BitsPerPlane = DeviceExtension->BitsPerPixel;
    Mode->Frequency = 60;

    /* Physical dimensions computed from 96 DPI assumption, rounded to nearest */
    Mode->XMillimeter =
        ((ULONGLONG)DeviceExtension->ScreenWidth * 254 + (Dpi * 5)) / (Dpi * 10);
    Mode->YMillimeter =
        ((ULONGLONG)DeviceExtension->ScreenHeight * 254 + (Dpi * 5)) / (Dpi * 10);

    Mode->NumberRedBits = 8;
    Mode->NumberGreenBits = 8;
    Mode->NumberBlueBits = 8;
    Mode->RedMask = DeviceExtension->RedMask;
    Mode->GreenMask = DeviceExtension->GreenMask;
    Mode->BlueMask = DeviceExtension->BlueMask;

    Mode->AttributeFlags = VIDEO_MODE_GRAPHICS |
                            VIDEO_MODE_COLOR |
                            VIDEO_MODE_NO_OFF_SCREEN;

    Mode->VideoMemoryBitmapWidth = DeviceExtension->ScreenWidth;
    Mode->VideoMemoryBitmapHeight = DeviceExtension->ScreenHeight;
    Mode->DriverSpecificAttributeFlags = 0;

    VideoPortDebugPrint(Error,
        "UefiFb: Initialize mode=%lux%lu stride=%ld bpp=%u pitch=%lu fb-bytes=%lu\n",
        Mode->VisScreenWidth,
        Mode->VisScreenHeight,
        Mode->ScreenStride,
        Mode->BitsPerPlane,
        DeviceExtension->PixelsPerScanLine,
        DeviceExtension->FrameBufferSize);

    return TRUE;
}

/*
 * UefiFbStartIO
 *
 * Processes Video Request Packets (IOCTLs from framebuf.dll).
 */
BOOLEAN NTAPI
UefiFbStartIO(
    PVOID HwDeviceExtension,
    PVIDEO_REQUEST_PACKET RequestPacket)
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

    VideoPortDebugPrint(Error,
        "UefiFb: StartIO ioctl=0x%lx in=%lu out=%lu mode=%lu mapped=%p\n",
        RequestPacket->IoControlCode,
        RequestPacket->InputBufferLength,
        RequestPacket->OutputBufferLength,
        DeviceExtension->CurrentMode,
        DeviceExtension->MappedFrameBuffer);

    switch (RequestPacket->IoControlCode)
    {
        /* ================================================================
         * IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES
         * Returns the number of available modes and size of each mode info.
         * ================================================================ */
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

        /* ================================================================
         * IOCTL_VIDEO_QUERY_AVAIL_MODES
         * Returns the single VIDEO_MODE_INFORMATION for our mode.
         * ================================================================ */
        case IOCTL_VIDEO_QUERY_AVAIL_MODES:
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

        /* ================================================================
         * IOCTL_VIDEO_QUERY_CURRENT_MODE
         * Returns the current mode information.
         * ================================================================ */
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

        /* ================================================================
         * IOCTL_VIDEO_SET_CURRENT_MODE
         * Validates the requested mode index (must be 0).
         * The UEFI framebuffer mode is already set by firmware.
         * ================================================================ */
        case IOCTL_VIDEO_SET_CURRENT_MODE:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MODE))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            VideoMode = (PVIDEO_MODE)RequestPacket->InputBuffer;
            if (VideoMode->RequestedMode != 0)
            {
                VideoPortDebugPrint(Error,
                    "UefiFb: Invalid mode %lu requested\n",
                    VideoMode->RequestedMode);
                Status = ERROR_INVALID_PARAMETER;
                break;
            }

            DeviceExtension->CurrentMode = 0;
            Status = NO_ERROR;
            break;

        /* ================================================================
         * IOCTL_VIDEO_RESET_DEVICE
         * Called when switching away from graphics mode. No-op for UEFI FB
         * since there is no VGA text mode to reset to.
         * ================================================================ */
        case IOCTL_VIDEO_RESET_DEVICE:
            Status = NO_ERROR;
            break;

        /* ================================================================
         * IOCTL_VIDEO_MAP_VIDEO_MEMORY
         * Maps the physical framebuffer into the caller's address space.
         * ================================================================ */
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
            inIoSpace = VIDEO_MEMORY_SPACE_MEMORY;

            MemoryInfo->VideoRamBase = VideoMemory->RequestedVirtualAddress;
            MemoryInfo->VideoRamLength = DeviceExtension->FrameBufferSize;

            Status = VideoPortMapMemory(
                DeviceExtension,
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

        /* ================================================================
         * IOCTL_VIDEO_UNMAP_VIDEO_MEMORY
         * Releases the framebuffer mapping.
         * ================================================================ */
        case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            VideoMemory = (PVIDEO_MEMORY)RequestPacket->InputBuffer;
            Status = VideoPortUnmapMemory(
                DeviceExtension,
                VideoMemory->RequestedVirtualAddress,
                NULL);

            DeviceExtension->MappedFrameBuffer = NULL;
            break;

        /* ================================================================
         * IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES
         * Reports no hardware cursor support. GDI will use a software cursor.
         * ================================================================ */
        case IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES:
            if (RequestPacket->OutputBufferLength <
                sizeof(VIDEO_POINTER_CAPABILITIES))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            PointerCaps =
                (PVIDEO_POINTER_CAPABILITIES)RequestPacket->OutputBuffer;
            PointerCaps->Flags = 0;
            PointerCaps->MaxWidth = 0;
            PointerCaps->MaxHeight = 0;
            PointerCaps->HWPtrBitmapStart = 0;
            PointerCaps->HWPtrBitmapEnd = 0;

            RequestPacket->StatusBlock->Information =
                sizeof(VIDEO_POINTER_CAPABILITIES);
            Status = NO_ERROR;
            break;

        /* ================================================================
         * Unsupported IOCTLs -- return ERROR_INVALID_FUNCTION.
         * framebuf.dll handles these gracefully.
         * ================================================================ */
        case IOCTL_VIDEO_SET_COLOR_REGISTERS:
        case IOCTL_VIDEO_DISABLE_POINTER:
        case IOCTL_VIDEO_SET_POINTER_ATTR:
        case IOCTL_VIDEO_SET_POINTER_POSITION:
        case IOCTL_VIDEO_QUERY_PUBLIC_ACCESS_RANGES:
        default:
            Status = ERROR_INVALID_FUNCTION;
            break;
    }

    RequestPacket->StatusBlock->Status = Status;
    return TRUE;
}

/*
 * UefiFbResetHw
 *
 * Called during bugcheck or shutdown. Return FALSE to delegate
 * reset to the HAL. The UEFI framebuffer cannot be "reset" to
 * text mode.
 */
BOOLEAN NTAPI
UefiFbResetHw(
    PVOID DeviceExtension,
    ULONG Columns,
    ULONG Rows)
{
    UNREFERENCED_PARAMETER(DeviceExtension);

    VideoPortDebugPrint(Error,
        "UefiFb: ResetHw columns=%lu rows=%lu\n",
        Columns,
        Rows);
    return FALSE;
}

/*
 * UefiFbGetPowerState
 *
 * Queries the current DPMS power state. Not supported for a generic
 * UEFI framebuffer.
 */
VP_STATUS NTAPI
UefiFbGetPowerState(
    PVOID HwDeviceExtension,
    ULONG HwId,
    PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);

    VideoPortDebugPrint(Error,
        "UefiFb: GetPowerState hwId=%lu control=%p\n",
        HwId,
        VideoPowerControl);
    return ERROR_DEV_NOT_EXIST;
}

/*
 * UefiFbSetPowerState
 *
 * Sets the DPMS power state. Not supported for a generic UEFI framebuffer.
 */
VP_STATUS NTAPI
UefiFbSetPowerState(
    PVOID HwDeviceExtension,
    ULONG HwId,
    PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);

    VideoPortDebugPrint(Error,
        "UefiFb: SetPowerState hwId=%lu control=%p\n",
        HwId,
        VideoPowerControl);
    return ERROR_DEV_NOT_EXIST;
}

/*
 * UefiFbGetVideoChildDescriptor
 *
 * Enumerates connected monitors. For UEFI FB we report a single
 * monitor with no EDID (no DDC/I2C on a generic framebuffer).
 */
VP_STATUS NTAPI
UefiFbGetVideoChildDescriptor(
    IN PVOID HwDeviceExtension,
    IN PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
    OUT PVIDEO_CHILD_TYPE VideoChildType,
    OUT PUCHAR pChildDescriptor,
    OUT PULONG UId,
    OUT PULONG pUnused)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(pUnused);

    VideoPortDebugPrint(Error,
        "UefiFb: GetVideoChildDescriptor index=%lu size=%lu child-ext=%p desc=%p\n",
        ChildEnumInfo ? ChildEnumInfo->ChildIndex : 0,
        ChildEnumInfo ? ChildEnumInfo->ChildDescriptorSize : 0,
        ChildEnumInfo ? ChildEnumInfo->ChildHwDeviceExtension : NULL,
        pChildDescriptor);

    if (ChildEnumInfo->ChildIndex == 0)
    {
        /* ACPI children -- none for this driver */
        VideoPortDebugPrint(Error,
            "UefiFb: GetVideoChildDescriptor -> no ACPI children\n");
        return ERROR_NO_MORE_DEVICES;
    }
    else if (ChildEnumInfo->ChildIndex == 1)
    {
        /* Single monitor, no EDID available */
        *VideoChildType = Monitor;
        *UId = 1;
        VideoPortDebugPrint(Error,
            "UefiFb: GetVideoChildDescriptor -> monitor uid=%lu\n",
            *UId);
        return VIDEO_ENUM_MORE_DEVICES;
    }
    else
    {
        /* No more children */
        VideoPortDebugPrint(Error,
            "UefiFb: GetVideoChildDescriptor -> end of enumeration\n");
        return ERROR_NO_MORE_DEVICES;
    }
}
