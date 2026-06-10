/*
 * PROJECT:     ReactOS Intel Alder Lake-N display miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PCI-bound videoport miniport for Intel N100/Alder Lake-N GOP
 *              framebuffer handoff.
 *
 * This is the first hardware-owned milestone: it binds the real PCI display
 * device and exposes the firmware-selected framebuffer through framebuf.dll.
 * It does not claim render-engine acceleration.
 */

#include "inteln100.h"

static BOOLEAN
IntelN100RangeContains(
    _In_ PVIDEO_ACCESS_RANGE Range,
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Length)
{
    ULONGLONG RangeStart;
    ULONGLONG RangeEnd;
    ULONGLONG AddressEnd;

    if (Range->RangeInIoSpace || Range->RangeLength == 0 || Length == 0)
        return FALSE;

    RangeStart = Range->RangeStart.QuadPart;
    RangeEnd = RangeStart + Range->RangeLength;
    AddressEnd = Address + Length;

    if (RangeEnd < RangeStart || AddressEnd < Address)
        return FALSE;

    return (Address >= RangeStart) && (AddressEnd <= RangeEnd);
}

static VOID
IntelN100StoreRange(
    _Out_ PINTELN100_RANGE Destination,
    _In_ PVIDEO_ACCESS_RANGE Source)
{
    Destination->Base = Source->RangeStart;
    Destination->Length = Source->RangeLength;
    Destination->InIoSpace = Source->RangeInIoSpace;
}

static VOID
IntelN100BuildModeInfo(
    _Inout_ PINTELN100_DEVICE_EXTENSION DeviceExtension)
{
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
}

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
    InitData.AdapterInterfaceType = PCIBus;
    InitData.HwFindAdapter = IntelN100FindAdapter;
    InitData.HwInitialize = IntelN100Initialize;
    InitData.HwStartIO = IntelN100StartIO;
    InitData.HwResetHw = IntelN100ResetHw;
    InitData.HwGetPowerState = IntelN100GetPowerState;
    InitData.HwSetPowerState = IntelN100SetPowerState;
    InitData.HwGetVideoChildDescriptor = IntelN100GetVideoChildDescriptor;
    InitData.HwDeviceExtensionSize = sizeof(INTELN100_DEVICE_EXTENSION);

    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}

VP_STATUS
NTAPI
IntelN100FindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again)
{
    PINTELN100_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    VIDEO_ACCESS_RANGE AccessRanges[INTELN100_MAX_ACCESS_RANGES];
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG BytesPerPixel;
    ULONGLONG VisibleFrameBufferSize;
    ULONGLONG FrameBufferEnd;
    ULONG i;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;
    VideoPortZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    VideoPortZeroMemory(AccessRanges, sizeof(AccessRanges));

    if (ConfigInfo->Length < sizeof(VIDEO_PORT_CONFIG_INFO))
        return ERROR_INVALID_PARAMETER;

    if (!InbvGetGopFrameBufferInfo(&FbInfo))
        return ERROR_DEV_NOT_EXIST;

    if (FbInfo.FrameBufferBase.QuadPart == 0 ||
        FbInfo.FrameBufferSize == 0 ||
        FbInfo.HorizontalResolution == 0 ||
        FbInfo.VerticalResolution == 0 ||
        FbInfo.PixelsPerScanLine == 0 ||
        FbInfo.PixelFormat == 0)
    {
        return ERROR_DEV_NOT_EXIST;
    }

    BytesPerPixel = (FbInfo.PixelFormat + 7) / 8;
    VisibleFrameBufferSize =
        (ULONGLONG)FbInfo.VerticalResolution *
        FbInfo.PixelsPerScanLine *
        BytesPerPixel;
    if (VisibleFrameBufferSize == 0 ||
        VisibleFrameBufferSize > FbInfo.FrameBufferSize ||
        VisibleFrameBufferSize > MAXULONG)
    {
        return ERROR_DEV_NOT_EXIST;
    }

    if (VideoPortGetAccessRanges(DeviceExtension,
                                 0,
                                 NULL,
                                 INTELN100_MAX_ACCESS_RANGES,
                                 AccessRanges,
                                 NULL,
                                 NULL,
                                 NULL) != NO_ERROR)
    {
        return ERROR_DEV_NOT_EXIST;
    }

    for (i = 0; i < INTELN100_MAX_ACCESS_RANGES; ++i)
    {
        if (AccessRanges[i].RangeLength == 0)
            continue;

        if (IntelN100RangeContains(&AccessRanges[i],
                                   FbInfo.FrameBufferBase.QuadPart,
                                   VisibleFrameBufferSize))
        {
            IntelN100StoreRange(&DeviceExtension->GraphicsAperture,
                                &AccessRanges[i]);
            continue;
        }

        if (!AccessRanges[i].RangeInIoSpace &&
            DeviceExtension->MmioRange.Length == 0)
        {
            IntelN100StoreRange(&DeviceExtension->MmioRange,
                                &AccessRanges[i]);
        }
    }

    if (DeviceExtension->GraphicsAperture.Length == 0)
    {
        VideoPortDebugPrint(Error,
            "IntelN100: GOP framebuffer 0x%I64x is not inside a PCI memory aperture\n",
            FbInfo.FrameBufferBase.QuadPart);
        return ERROR_DEV_NOT_EXIST;
    }

    FrameBufferEnd = FbInfo.FrameBufferBase.QuadPart + VisibleFrameBufferSize;
    if (FrameBufferEnd <
        (ULONGLONG)DeviceExtension->GraphicsAperture.Base.QuadPart)
    {
        return ERROR_DEV_NOT_EXIST;
    }

    DeviceExtension->FrameBufferPhysical = FbInfo.FrameBufferBase;
    DeviceExtension->FrameBufferSize = (ULONG)VisibleFrameBufferSize;
    DeviceExtension->ScreenWidth = FbInfo.HorizontalResolution;
    DeviceExtension->ScreenHeight = FbInfo.VerticalResolution;
    DeviceExtension->PixelsPerScanLine = FbInfo.PixelsPerScanLine;
    DeviceExtension->BitsPerPixel = FbInfo.PixelFormat;
    DeviceExtension->BytesPerScanLine =
        DeviceExtension->PixelsPerScanLine * BytesPerPixel;
    DeviceExtension->RedMask = FbInfo.RedMask;
    DeviceExtension->GreenMask = FbInfo.GreenMask;
    DeviceExtension->BlueMask = FbInfo.BlueMask;
    DeviceExtension->CurrentMode = 0;

    ConfigInfo->NumEmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntries = NULL;
    ConfigInfo->EmulatorAccessEntriesContext = 0;
    ConfigInfo->HardwareStateSize = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress =
        DeviceExtension->FrameBufferPhysical;
    ConfigInfo->VdmPhysicalVideoMemoryLength =
        DeviceExtension->FrameBufferSize;

    return NO_ERROR;
}

BOOLEAN
NTAPI
IntelN100Initialize(
    _In_ PVOID HwDeviceExtension)
{
    PINTELN100_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    WCHAR ChipType[] = L"Intel Alder Lake-N";
    ULONG SizeInBytes = DeviceExtension->GraphicsAperture.Length;

    IntelN100BuildModeInfo(DeviceExtension);

    VideoPortSetRegistryParameters(DeviceExtension,
                                   L"HardwareInformation.ChipType",
                                   ChipType,
                                   sizeof(ChipType));
    VideoPortSetRegistryParameters(DeviceExtension,
                                   L"HardwareInformation.MemorySize",
                                   &SizeInBytes,
                                   sizeof(SizeInBytes));

    return TRUE;
}

VP_STATUS
NTAPI
IntelN100GetVideoChildDescriptor(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
    _Out_ PVIDEO_CHILD_TYPE VideoChildType,
    _Out_ PUCHAR ChildDescriptor,
    _Out_ PULONG UId,
    _Out_ PULONG Unused)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(ChildDescriptor);

    if (ChildEnumInfo == NULL ||
        VideoChildType == NULL ||
        UId == NULL ||
        Unused == NULL ||
        ChildEnumInfo->Size != sizeof(VIDEO_CHILD_ENUM_INFO))
    {
        return VIDEO_ENUM_INVALID_DEVICE;
    }

    *Unused = 0;

    if (ChildEnumInfo->ChildIndex == 0)
        return VIDEO_ENUM_INVALID_DEVICE;

    if (ChildEnumInfo->ChildIndex == DISPLAY_ADAPTER_HW_ID)
    {
        *VideoChildType = VideoChip;
        *UId = DISPLAY_ADAPTER_HW_ID;
        return VIDEO_ENUM_MORE_DEVICES;
    }

    if (ChildEnumInfo->ChildIndex == 1)
    {
        *VideoChildType = Monitor;
        *UId = 1;
        return VIDEO_ENUM_MORE_DEVICES;
    }

    return VIDEO_ENUM_NO_MORE_DEVICES;
}

BOOLEAN
NTAPI
IntelN100StartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket)
{
    PINTELN100_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    VP_STATUS Status = ERROR_INVALID_FUNCTION;
    PVIDEO_MODE_INFORMATION ModeInfo;
    PVIDEO_MEMORY VideoMemory;
    PVIDEO_MEMORY_INFORMATION MemoryInfo;
    PVIDEO_MODE VideoMode;
    PVIDEO_NUM_MODES NumModes;
    PVIDEO_POINTER_CAPABILITIES PointerCaps;
    PHYSICAL_ADDRESS FrameBuffer;
    ULONG InIoSpace;

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
            VideoPortMoveMemory(ModeInfo,
                                &DeviceExtension->ModeInfo,
                                sizeof(VIDEO_MODE_INFORMATION));
            RequestPacket->StatusBlock->Information =
                sizeof(VIDEO_MODE_INFORMATION);
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_SET_CURRENT_MODE:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MODE))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            VideoMode = (PVIDEO_MODE)RequestPacket->InputBuffer;
            if ((VideoMode->RequestedMode & 0x3fffffff) != 0)
            {
                Status = ERROR_INVALID_PARAMETER;
                break;
            }
            DeviceExtension->CurrentMode = 0;
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_RESET_DEVICE:
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
            InIoSpace = VIDEO_MEMORY_SPACE_MEMORY;
            MemoryInfo->VideoRamBase = VideoMemory->RequestedVirtualAddress;
            MemoryInfo->VideoRamLength = DeviceExtension->FrameBufferSize;
            Status = VideoPortMapMemory(DeviceExtension,
                                        FrameBuffer,
                                        &MemoryInfo->VideoRamLength,
                                        &InIoSpace,
                                        &MemoryInfo->VideoRamBase);
            if (Status == NO_ERROR)
            {
                MemoryInfo->FrameBufferBase = MemoryInfo->VideoRamBase;
                MemoryInfo->FrameBufferLength = MemoryInfo->VideoRamLength;
                DeviceExtension->MappedFrameBuffer = MemoryInfo->VideoRamBase;
                RequestPacket->StatusBlock->Information =
                    sizeof(VIDEO_MEMORY_INFORMATION);
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
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_POINTER_CAPABILITIES))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            PointerCaps = (PVIDEO_POINTER_CAPABILITIES)RequestPacket->OutputBuffer;
            VideoPortZeroMemory(PointerCaps, sizeof(*PointerCaps));
            RequestPacket->StatusBlock->Information =
                sizeof(VIDEO_POINTER_CAPABILITIES);
            Status = NO_ERROR;
            break;

        default:
            Status = ERROR_INVALID_FUNCTION;
            break;
    }

    RequestPacket->StatusBlock->Status = Status;
    return TRUE;
}

BOOLEAN
NTAPI
IntelN100ResetHw(
    _In_ PVOID DeviceExtension,
    _In_ ULONG Columns,
    _In_ ULONG Rows)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(Columns);
    UNREFERENCED_PARAMETER(Rows);
    return FALSE;
}

VP_STATUS
NTAPI
IntelN100GetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);

    if (VideoPowerControl == NULL)
        return ERROR_INVALID_PARAMETER;

    VideoPowerControl->PowerState = VideoPowerOn;
    return NO_ERROR;
}

VP_STATUS
NTAPI
IntelN100SetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);

    if (VideoPowerControl == NULL)
        return ERROR_INVALID_PARAMETER;

    if (VideoPowerControl->PowerState == VideoPowerOn)
        return NO_ERROR;

    return ERROR_INVALID_FUNCTION;
}
