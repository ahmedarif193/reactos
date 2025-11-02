#include "uefifb.h"
#include "../../../include/uefifb/uefifb_ioctl.h"

#define UEFIFB_MODE_INDEX 0

static const UCHAR UefiFbSyntheticEdid[128] = {
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00, 0x4C,0x2D,0xA0,0x01,0x01,0x01,0x01,0x01,
    0x01,0x15,0x01,0x03,0x80,0x1A,0x11,0x78, 0x0A,0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,
    0x0F,0x50,0x54,0xAD,0x00,0x81,0x80,0x81, 0x40,0x81,0x00,0x95,0x00,0xA9,0x40,0xB3,
    0x00,0x01,0x01,0x01,0x01,0x02,0x3A,0x80, 0x18,0x71,0x38,0x2D,0x40,0x58,0x2C,0x45,
    0x00,0xFD,0x1E,0x11,0x00,0x00,0x1E,0x00, 0x00,0x00,0xFC,0x00,0x52,0x4F,0x53,0x20,
    0x55,0x45,0x46,0x49,0x20,0x46,0x42,0x0A, 0x00,0x00,0x00,0xFF,0x00,0x30,0x30,0x30,
    0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x0A, 0x00,0x00,0x00,0xFD,0x00,0x3A,0x3E,0x0F,
    0x2E,0x08,0x00,0x0A,0x20,0x20,0x20,0x20, 0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x2E
};

static ULONG
UefiFbCountMaskBits(_In_ ULONG Mask)
{
    ULONG Count = 0;

    while (Mask != 0)
    {
        Count += Mask & 1u;
        Mask >>= 1;
    }

    return Count;
}

static BOOLEAN
UefiFbPopulateModeInformation(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    const LOADER_PARAMETER_FRAMEBUFFER *Fb = &DevExt->FrameBufferInfo;
    PVIDEO_MODE_INFORMATION Mode = &DevExt->ModeInfo;
    ULONG RedMask, GreenMask, BlueMask;
    ULONG BitsPerPixel;
    ULONG PixelsPerScanLine;

    switch (Fb->PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor:
            RedMask = 0x000000FFu;
            GreenMask = 0x0000FF00u;
            BlueMask = 0x00FF0000u;
            BitsPerPixel = 32;
            break;

        case PixelBlueGreenRedReserved8BitPerColor:
            RedMask = 0x00FF0000u;
            GreenMask = 0x0000FF00u;
            BlueMask = 0x000000FFu;
            BitsPerPixel = 32;
            break;

        case PixelBitMask:
            RedMask = Fb->RedMask;
            GreenMask = Fb->GreenMask;
            BlueMask = Fb->BlueMask;
            BitsPerPixel = UefiFbCountMaskBits(RedMask | GreenMask | BlueMask);
            if (BitsPerPixel == 0)
                return FALSE;
            BitsPerPixel = (BitsPerPixel + 7u) & ~7u;
            break;

        default:
            return FALSE;
    }

    PixelsPerScanLine = Fb->PixelsPerScanLine ? Fb->PixelsPerScanLine : Fb->HorizontalResolution;

    VideoPortZeroMemory(Mode, sizeof(*Mode));
    Mode->Length = sizeof(*Mode);
    Mode->ModeIndex = UEFIFB_MODE_INDEX;
    Mode->VisScreenWidth = Fb->HorizontalResolution;
    Mode->VisScreenHeight = Fb->VerticalResolution;
    Mode->ScreenStride = PixelsPerScanLine * (BitsPerPixel / 8);
    Mode->NumberOfPlanes = 1;
    Mode->BitsPerPlane = BitsPerPixel;
    Mode->Frequency = 60;
    Mode->NumberRedBits = UefiFbCountMaskBits(RedMask);
    Mode->NumberGreenBits = UefiFbCountMaskBits(GreenMask);
    Mode->NumberBlueBits = UefiFbCountMaskBits(BlueMask);
    Mode->RedMask = RedMask;
    Mode->GreenMask = GreenMask;
    Mode->BlueMask = BlueMask;
    Mode->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS | VIDEO_MODE_LINEAR;
    Mode->VideoMemoryBitmapWidth = Fb->HorizontalResolution;
    Mode->VideoMemoryBitmapHeight = Fb->VerticalResolution;
    Mode->DriverSpecificAttributeFlags = 0;

    return TRUE;
}

static BOOLEAN
UefiFbBuildModeTable(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt)
{
    ULONG count = 0, i, built = 0;
    LOADER_PARAMETER_FRAMEBUFFER fb;

    /* Query mode count (stub returns 1 for now) */
    if (!InbvQueryGopModeCount(&count) || count == 0)
        count = 1;

    DevExt->ModeTable = (PVIDEO_MODE_INFORMATION)
        VideoPortAllocatePool(DevExt, 0, sizeof(VIDEO_MODE_INFORMATION) * count, 'bfEU');
    if (!DevExt->ModeTable)
        return FALSE;

    /* Build table entries from firmware info */
    for (i = 0; i < count; ++i)
    {
        if (!InbvQueryGopModeInfo(i, &fb))
            break;
        DevExt->FrameBufferInfo = fb;
        if (!UefiFbPopulateModeInformation(DevExt))
            break;
        VideoPortMoveMemory(&DevExt->ModeTable[i], &DevExt->ModeInfo, sizeof(VIDEO_MODE_INFORMATION));
        DevExt->ModeTable[i].ModeIndex = i;
        built++;
    }

    /* Restore current FB info */
    InbvGetGopFrameBufferInfo(&DevExt->FrameBufferInfo);

    DevExt->ModeCount = (built != 0) ? built : 1;
    DevExt->CurrentModeIndex = 0;
    return TRUE;
}

static BOOLEAN NTAPI
UefiFbSetCurrentMode(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                     _In_ ULONG RequestedMode,
                     _Out_ PSTATUS_BLOCK StatusBlock)
{
    if (RequestedMode >= DevExt->ModeCount)
    {
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    if (RequestedMode == DevExt->CurrentModeIndex)
    {
        DevExt->ModeSet = TRUE;
        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    /* Try firmware mode switch via INBV */
    if (InbvSetGopMode(RequestedMode))
    {
        /* Tear down any existing mapping (we are at PASSIVE_LEVEL here) */
        if (DevExt->MappedFrameBuffer)
        {
            (VOID)VideoPortUnmapMemory(DevExt, DevExt->MappedFrameBuffer, NULL);
            DevExt->MappedFrameBuffer = NULL;
            DevExt->MappedLength = 0;
        }

        /* Refresh FB info and rebuild mode info/table */
        if (!InbvGetGopFrameBufferInfo(&DevExt->FrameBufferInfo) ||
            !UefiFbPopulateModeInformation(DevExt))
        {
            StatusBlock->Status = ERROR_DEV_NOT_EXIST;
            return FALSE;
        }

        if (DevExt->ModeTable)
        {
            VideoPortFreePool(DevExt, DevExt->ModeTable);
            DevExt->ModeTable = NULL;
            DevExt->ModeCount = 0;
        }
        (void)UefiFbBuildModeTable(DevExt);

        DevExt->CurrentModeIndex = RequestedMode;
        DevExt->ModeSet = TRUE;
        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    /* Firmware refused or not supported yet */
    StatusBlock->Status = ERROR_INVALID_FUNCTION;
    return FALSE;
}

static BOOLEAN NTAPI
UefiFbQueryCurrentMode(_In_ PUEFIFB_DEVICE_EXTENSION DevExt,
                       _Out_ PVIDEO_MODE_INFORMATION ModeInfo,
                       _Out_ PSTATUS_BLOCK StatusBlock)
{
    VideoPortMoveMemory(ModeInfo, &DevExt->ModeInfo, sizeof(*ModeInfo));

    StatusBlock->Information = sizeof(*ModeInfo);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static BOOLEAN NTAPI
UefiFbQueryAvailableModes(_In_ PUEFIFB_DEVICE_EXTENSION DevExt,
                         _Out_writes_bytes_(OutputBytes) PVIDEO_MODE_INFORMATION ModeInfo,
                         _In_ ULONG OutputBytes,
                         _Out_ PSTATUS_BLOCK StatusBlock)
{
    ULONG need;
    if (DevExt->ModeCount == 0 || DevExt->ModeTable == NULL)
    {
        StatusBlock->Status = ERROR_DEV_NOT_EXIST;
        return FALSE;
    }

    need = DevExt->ModeCount * sizeof(VIDEO_MODE_INFORMATION);
    if (OutputBytes < need)
    {
        StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
        return FALSE;
    }

    VideoPortMoveMemory(ModeInfo, DevExt->ModeTable, need);
    StatusBlock->Information = need;
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static BOOLEAN NTAPI
UefiFbQueryNumModes(_In_ PUEFIFB_DEVICE_EXTENSION DevExt,
                    _Out_ PVIDEO_NUM_MODES NumModes,
                    _Out_ PSTATUS_BLOCK StatusBlock)
{
    NumModes->NumModes = (DevExt->ModeCount != 0) ? DevExt->ModeCount : 1;
    NumModes->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);

    StatusBlock->Information = sizeof(*NumModes);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static BOOLEAN NTAPI
UefiFbMapVideoMemory(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                     _Inout_ PVIDEO_MEMORY RequestedAddress,
                     _Out_ PVIDEO_MEMORY_INFORMATION MapInfo,
                     _Out_ PSTATUS_BLOCK StatusBlock)
{
    /* Ensure we only map at PASSIVE_LEVEL per XPDM contract */
    if (VideoPortGetCurrentIrql() != 0) /* PASSIVE_LEVEL */
    {
        StatusBlock->Status = ERROR_INVALID_FUNCTION;
        return FALSE;
    }

    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Length;
    ULONG InIoSpace = VIDEO_MEMORY_SPACE_MEMORY;
    VP_STATUS Status;

    /* Fast-path: already mapped, just return the same mapping */
    if (DevExt->MappedFrameBuffer != NULL)
    {
        MapInfo->VideoRamBase = DevExt->MappedFrameBuffer;
        MapInfo->VideoRamLength = DevExt->MappedLength;
        MapInfo->FrameBufferBase = DevExt->MappedFrameBuffer;
        MapInfo->FrameBufferLength = DevExt->MappedLength;

        StatusBlock->Information = sizeof(*MapInfo);
        StatusBlock->Status = NO_ERROR;
        return TRUE;
    }

    PhysicalAddress.QuadPart = DevExt->FrameBufferInfo.FrameBufferBase.QuadPart;
    Length = DevExt->FrameBufferInfo.FrameBufferSize;

    MapInfo->VideoRamBase = RequestedAddress->RequestedVirtualAddress;
    MapInfo->VideoRamLength = Length;

    Status = VideoPortMapMemory(DevExt,
                                PhysicalAddress,
                                &MapInfo->VideoRamLength,
                                &InIoSpace,
                                &MapInfo->VideoRamBase);
    if (Status != NO_ERROR)
    {
        StatusBlock->Status = Status;
        return FALSE;
    }

    DevExt->MappedFrameBuffer = MapInfo->VideoRamBase;
    DevExt->MappedLength = MapInfo->VideoRamLength;

    MapInfo->FrameBufferBase = MapInfo->VideoRamBase;
    MapInfo->FrameBufferLength = MapInfo->VideoRamLength;

    StatusBlock->Information = sizeof(*MapInfo);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static BOOLEAN NTAPI
UefiFbUnmapVideoMemory(_Inout_ PUEFIFB_DEVICE_EXTENSION DevExt,
                       _In_ PVIDEO_MEMORY VideoMemory,
                       _Out_ PSTATUS_BLOCK StatusBlock)
{
    /* Ensure we only unmap at PASSIVE_LEVEL per XPDM contract */
    if (VideoPortGetCurrentIrql() != 0) /* PASSIVE_LEVEL */
    {
        StatusBlock->Status = ERROR_INVALID_FUNCTION;
        return FALSE;
    }

    VP_STATUS Status;

    /* Only attempt to unmap the mapping we created */
    if (DevExt->MappedFrameBuffer == NULL ||
        VideoMemory->RequestedVirtualAddress == NULL ||
        VideoMemory->RequestedVirtualAddress != DevExt->MappedFrameBuffer)
    {
        StatusBlock->Status = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    Status = VideoPortUnmapMemory(DevExt, VideoMemory->RequestedVirtualAddress, NULL);
    if (Status == NO_ERROR)
    {
        DevExt->MappedFrameBuffer = NULL;
        DevExt->MappedLength = 0;
    }

    StatusBlock->Status = Status;
    return (Status == NO_ERROR);
}

static BOOLEAN NTAPI
UefiFbQueryColorCapabilities(_Out_ PVIDEO_COLOR_CAPABILITIES Caps,
                             _Out_ PSTATUS_BLOCK StatusBlock)
{
    VideoPortZeroMemory(Caps, sizeof(*Caps));

    Caps->Length = sizeof(*Caps);
    Caps->AttributeFlags = VIDEO_DEVICE_COLOR;
    Caps->WhiteChromaticity_x = 3127;
    Caps->WhiteChromaticity_y = 3290;
    Caps->RedChromaticity_x = 6700;
    Caps->RedChromaticity_y = 3300;
    Caps->GreenChromaticity_x = 2100;
    Caps->GreenChromaticity_y = 7100;
    Caps->BlueChromaticity_x = 1400;
    Caps->BlueChromaticity_y = 800;
    Caps->WhiteGamma = 20000;
    Caps->RedGamma = 20000;
    Caps->GreenGamma = 20000;
    Caps->BlueGamma = 20000;

    StatusBlock->Information = sizeof(*Caps);
    StatusBlock->Status = NO_ERROR;
    return TRUE;
}

static VP_STATUS NTAPI
UefiFbSetPowerState(_In_ PVOID HwDeviceExtension,
                    _In_ ULONG HwId,
                    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);
    UNREFERENCED_PARAMETER(VideoPowerControl);

    return NO_ERROR;
}

static VP_STATUS NTAPI
UefiFbGetPowerState(_In_ PVOID HwDeviceExtension,
                    _In_ ULONG HwId,
                    _Out_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);

    if (VideoPowerControl == NULL)
        return ERROR_INVALID_PARAMETER;

    VideoPowerControl->Length = sizeof(*VideoPowerControl);
    VideoPowerControl->DPMSVersion = 0x0100;
    VideoPowerControl->PowerState = VideoPowerOn;

    return NO_ERROR;
}

static BOOLEAN NTAPI
UefiFbResetHw(_In_ PVOID HwDeviceExtension,
              _In_ ULONG Columns,
              _In_ ULONG Rows)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(Columns);
    UNREFERENCED_PARAMETER(Rows);
    return TRUE;
}

static VP_STATUS NTAPI
UefiFbGetVideoChildDescriptor(_In_ PVOID HwDeviceExtension,
                              _In_ PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
                              _Out_ PVIDEO_CHILD_TYPE VideoChildType,
                              _Out_writes_bytes_(ChildEnumInfo->ChildDescriptorSize) PUCHAR ChildDescriptor,
                              _Out_ PULONG UId,
                              _Out_ PULONG Unused)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);

    if (ChildEnumInfo->ChildIndex > 0)
        return ERROR_NO_MORE_DEVICES;

    if (ChildEnumInfo->ChildDescriptorSize < sizeof(UefiFbSyntheticEdid))
        return ERROR_INSUFFICIENT_BUFFER;

    *VideoChildType = Monitor;
    VideoPortMoveMemory(ChildDescriptor, (PVOID)UefiFbSyntheticEdid, sizeof(UefiFbSyntheticEdid));

    if (UId)
        *UId = 0;
    if (Unused)
        *Unused = 0;

    return NO_ERROR;
}

static BOOLEAN NTAPI
UefiFbStartIO(_In_ PVOID HwDeviceExtension,
              _Inout_ PVIDEO_REQUEST_PACKET RequestPacket)
{
    PUEFIFB_DEVICE_EXTENSION DevExt = HwDeviceExtension;

    switch (RequestPacket->IoControlCode)
    {
        case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES:
        {
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_NUM_MODES))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbQueryNumModes(DevExt,
                                       (PVIDEO_NUM_MODES)RequestPacket->OutputBuffer,
                                       RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_QUERY_AVAIL_MODES:
        {
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbQueryAvailableModes(DevExt,
                                             (PVIDEO_MODE_INFORMATION)RequestPacket->OutputBuffer,
                                             RequestPacket->OutputBufferLength,
                                             RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_QUERY_CURRENT_MODE:
        {
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbQueryCurrentMode(DevExt,
                                          (PVIDEO_MODE_INFORMATION)RequestPacket->OutputBuffer,
                                          RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_SET_CURRENT_MODE:
        {
            ULONG RequestedMode;

            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MODE))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            RequestedMode = ((PVIDEO_MODE)RequestPacket->InputBuffer)->RequestedMode;
            return UefiFbSetCurrentMode(DevExt, RequestedMode, RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_MAP_VIDEO_MEMORY:
        {
            if ((RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY)) ||
                (RequestPacket->OutputBufferLength < sizeof(VIDEO_MEMORY_INFORMATION)))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbMapVideoMemory(DevExt,
                                        (PVIDEO_MEMORY)RequestPacket->InputBuffer,
                                        (PVIDEO_MEMORY_INFORMATION)RequestPacket->OutputBuffer,
                                        RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
        {
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbUnmapVideoMemory(DevExt,
                                          (PVIDEO_MEMORY)RequestPacket->InputBuffer,
                                          RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES:
        {
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_COLOR_CAPABILITIES))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            return UefiFbQueryColorCapabilities((PVIDEO_COLOR_CAPABILITIES)RequestPacket->OutputBuffer,
                                                RequestPacket->StatusBlock);
        }

        case IOCTL_VIDEO_RESET_DEVICE:
        {
            RequestPacket->StatusBlock->Status = NO_ERROR;
            return TRUE;
        }

        /* Optional capability query: advertise UEFI linear-only */
        case IOCTL_VIDEO_UEFIFB_QUERY_CAPS:
        {
            if (RequestPacket->OutputBufferLength < sizeof(UEFIFB_CAPS))
            {
                RequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return FALSE;
            }

            PUEFIFB_CAPS caps = (PUEFIFB_CAPS)RequestPacket->OutputBuffer;
            caps->Version = UEFIFB_CAPS_VERSION;
            caps->Caps = UEFIFB_CAP_LINEAR_ONLY;
            RequestPacket->StatusBlock->Information = sizeof(*caps);
            RequestPacket->StatusBlock->Status = NO_ERROR;
            return TRUE;
        }

        default:
            break;
    }

    RequestPacket->StatusBlock->Status = ERROR_INVALID_FUNCTION;
    return FALSE;
}

static BOOLEAN NTAPI
UefiFbInitialize(_In_ PVOID HwDeviceExtension)
{
    PUEFIFB_DEVICE_EXTENSION DevExt = HwDeviceExtension;

    DevExt->CurrentModeIndex = UEFIFB_MODE_INDEX;
    DevExt->ModeSet = FALSE;
    return TRUE;
}

static VP_STATUS NTAPI
UefiFbFindAdapter(_In_ PVOID HwDeviceExtension,
                  _In_opt_ PVOID HwContext,
                  _In_opt_ PWSTR ArgumentString,
                  _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
                  _Out_ PUCHAR Again)
{
    PUEFIFB_DEVICE_EXTENSION DevExt = HwDeviceExtension;
    LOADER_PARAMETER_FRAMEBUFFER Fb;
    static const WCHAR AdapterString[] = L"UEFI GOP Framebuffer";
    static const WCHAR ChipType[] = L"Firmware GOP";
    static const WCHAR DacType[] = L"Internal DAC";

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(ArgumentString);

    if (ConfigInfo->Length < sizeof(*ConfigInfo))
        return ERROR_INVALID_PARAMETER;

    if (!InbvGetGopFrameBufferInfo(&Fb))
        return ERROR_DEV_NOT_EXIST;

    if ((Fb.FrameBufferBase.QuadPart == 0) ||
        (Fb.FrameBufferSize == 0) ||
        (Fb.HorizontalResolution == 0) ||
        (Fb.VerticalResolution == 0))
    {
        return ERROR_DEV_NOT_EXIST;
    }

    DevExt->FrameBufferInfo = Fb;
    DevExt->ModeCount = 0;
    DevExt->ModeTable = NULL;
    DevExt->MappedFrameBuffer = NULL;
    DevExt->MappedLength = 0;
    DevExt->ModeSet = FALSE;
    DevExt->CurrentModeIndex = 0;

    if (!UefiFbPopulateModeInformation(DevExt))
        return ERROR_DEV_NOT_EXIST;

    if (!UefiFbBuildModeTable(DevExt))
        return ERROR_NOT_ENOUGH_MEMORY;

    ConfigInfo->NumEmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntries = NULL;
    ConfigInfo->EmulatorAccessEntriesContext = 0;
    ConfigInfo->HardwareStateSize = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress.QuadPart = Fb.FrameBufferBase.QuadPart;
    ConfigInfo->VdmPhysicalVideoMemoryLength = Fb.FrameBufferSize;
    ConfigInfo->SystemIoBusNumber = 0;
    ConfigInfo->AdapterInterfaceType = Internal;

    *Again = 0;

    VideoPortSetRegistryParameters(DevExt,
                                   L"HardwareInformation.AdapterString",
                                   (PVOID)AdapterString,
                                   sizeof(AdapterString));
    VideoPortSetRegistryParameters(DevExt,
                                   L"HardwareInformation.ChipType",
                                   (PVOID)ChipType,
                                   sizeof(ChipType));
    VideoPortSetRegistryParameters(DevExt,
                                   L"HardwareInformation.DacType",
                                   (PVOID)DacType,
                                   sizeof(DacType));
    VideoPortSetRegistryParameters(DevExt,
                                   L"HardwareInformation.MemorySize",
                                   &Fb.FrameBufferSize,
                                   sizeof(Fb.FrameBufferSize));

    return NO_ERROR;
}

ULONG
NTAPI
DriverEntry(_In_ PVOID Context1,
            _In_ PVOID Context2)
{
    VIDEO_HW_INITIALIZATION_DATA InitData;

    VideoPortZeroMemory(&InitData, sizeof(InitData));

    InitData.HwInitDataSize = sizeof(InitData);
    InitData.HwFindAdapter = UefiFbFindAdapter;
    InitData.HwInitialize = UefiFbInitialize;
    InitData.HwStartIO = UefiFbStartIO;
    InitData.HwResetHw = UefiFbResetHw;
    InitData.HwGetPowerState = UefiFbGetPowerState;
    InitData.HwSetPowerState = UefiFbSetPowerState;
    InitData.HwGetVideoChildDescriptor = UefiFbGetVideoChildDescriptor;
    InitData.HwDeviceExtensionSize = sizeof(UEFIFB_DEVICE_EXTENSION);
    InitData.HwLegacyResourceList = NULL;
    InitData.HwLegacyResourceCount = 0;

    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}
