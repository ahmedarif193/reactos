/*
 * PROJECT:     ReactOS Raspberry Pi 5 framebuffer miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RPi5-owned videoport miniport for the firmware GOP framebuffer.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5vc4.h"
#include "rpi5vc4_hvs.h"
#include "rpi5vc4_crtc.h"

#define RPI5VC4_ACPI_SIGNATURE(a, b, c, d) \
    ((ULONG)(a) | ((ULONG)(b) << 8) | ((ULONG)(c) << 16) | ((ULONG)(d) << 24))

#define RPI5VC4_ACPI_FADT RPI5VC4_ACPI_SIGNATURE('F', 'A', 'C', 'P')

static BOOLEAN
Rpi5Vc4IsRpi5Platform(VOID)
{
    if (HalGetCachedAcpiTable == NULL)
        return FALSE;

    return HalGetCachedAcpiTable(RPI5VC4_ACPI_FADT, "RPIFDN", "RPI5") != NULL;
}

static VP_STATUS
Rpi5Vc4LoadGopInfo(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG BytesPerPixel;
    ULONGLONG VisibleFrameBufferSize;

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

    DeviceExtension->FirmwareFrameBufferPhysical = FbInfo.FrameBufferBase;
    DeviceExtension->FrameBufferPhysical = FbInfo.FrameBufferBase;
    DeviceExtension->FrameBufferVirtual = NULL;
    DeviceExtension->FrameBufferSize = (ULONG)VisibleFrameBufferSize;
    DeviceExtension->ScreenWidth = FbInfo.HorizontalResolution;
    DeviceExtension->ScreenHeight = FbInfo.VerticalResolution;
    DeviceExtension->PixelsPerScanLine = FbInfo.PixelsPerScanLine;
    DeviceExtension->BitsPerPixel = FbInfo.PixelFormat;
    DeviceExtension->RedMask = FbInfo.RedMask;
    DeviceExtension->GreenMask = FbInfo.GreenMask;
    DeviceExtension->BlueMask = FbInfo.BlueMask;
    DeviceExtension->ReservedMask = FbInfo.Reserved;
    DeviceExtension->BytesPerScanLine =
        DeviceExtension->PixelsPerScanLine * BytesPerPixel;
    DeviceExtension->MappedFrameBuffer = NULL;
    DeviceExtension->CurrentMode = 0;

    return NO_ERROR;
}

static VOID
Rpi5Vc4InitFrameBuffer(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    /*
     * DISABLED - scan out the firmware GOP framebuffer directly.
     *
     * A private scanout buffer was tried here via
     * MmAllocateContiguousMemorySpecifyCache(MmWriteCombined). On ARM64 that
     * leaves a cacheable kernel-linear alias of the same physical pages. The
     * mismatched memory attributes let stale lines from the cacheable alias
     * write back over GDI's write-combined draws, so the (non-coherent) HVS
     * scans out erratic stale patches even with the mouse stationary.
     *
     * The firmware framebuffer is reserved by the loader as
     * LoaderFirmwarePermanent (an "invisible" type), so the kernel never frees,
     * zeroes, or KSEG0-aliases it - the write-combining videoport mapping is its
     * only view and it stays coherent with the HVS. Keep using it until a
     * private, alias-free scanout buffer (and page-flip) is implemented.
     */
    UNREFERENCED_PARAMETER(DeviceExtension);
}

static VOID
Rpi5Vc4BuildModeInfo(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
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
    Mode->DriverSpecificAttributeFlags = DeviceExtension->ReservedMask;
    Mode->AttributeFlags = VIDEO_MODE_GRAPHICS |
                           VIDEO_MODE_COLOR |
                           VIDEO_MODE_NO_OFF_SCREEN;
    Mode->VideoMemoryBitmapWidth = DeviceExtension->ScreenWidth;
    Mode->VideoMemoryBitmapHeight = DeviceExtension->ScreenHeight;
}

ULONG
NTAPI
DriverEntry(
    _In_ PVOID Context1,
    _In_ PVOID Context2)
{
    VIDEO_HW_INITIALIZATION_DATA InitData;

    if (!Rpi5Vc4IsRpi5Platform())
    {
        /*
         * The ARM64 image is shared with generic UEFI machines.  Decline before
         * entering videoport so no \Device\VideoN slot is consumed and uefifb can
         * handle the firmware framebuffer.
         */
        return STATUS_SUCCESS;
    }

    VideoPortZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
    InitData.StartingDeviceNumber = 0;
    InitData.AdapterInterfaceType = Internal;
    InitData.HwFindAdapter = Rpi5Vc4FindAdapter;
    InitData.HwInitialize = Rpi5Vc4Initialize;
    InitData.HwStartIO = Rpi5Vc4StartIO;
    InitData.HwResetHw = Rpi5Vc4ResetHw;
    InitData.HwGetPowerState = Rpi5Vc4GetPowerState;
    InitData.HwSetPowerState = Rpi5Vc4SetPowerState;
    InitData.HwGetVideoChildDescriptor = NULL;
    InitData.HwDeviceExtensionSize = sizeof(RPI5VC4_DEVICE_EXTENSION);

    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}

VP_STATUS
NTAPI
Rpi5Vc4FindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension =
        (PRPI5VC4_DEVICE_EXTENSION)HwDeviceExtension;
    VP_STATUS Status;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;
    VideoPortZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    if (ConfigInfo->Length < sizeof(VIDEO_PORT_CONFIG_INFO))
        return ERROR_INVALID_PARAMETER;

    Status = Rpi5Vc4LoadGopInfo(DeviceExtension);
    if (Status != NO_ERROR)
        return Status;

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

/* Allocate the hardware cursor surface used by the HVS overlay plane. */
static VOID
Rpi5Vc4InitCursor(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    const SIZE_T Bytes = RPI5VC4_CURSOR_WIDTH * RPI5VC4_CURSOR_HEIGHT * sizeof(ULONG);

    if (DeviceExtension->CursorVa != NULL)
        return;

    /*
     * Allocate above the firmware framebuffer (which lives just below 1GB at
     * 0x3F400000 and is the one address we know the HVS scans). Low DRAM is
     * often VPU-reserved and not in the HVS's view, so force a high buffer.
     */
    Low.QuadPart = 0x40000000ULL;
    High.QuadPart = 0xFFFFFFFFFFULL;   /* 40-bit DMA reach of the HVS */
    Boundary.QuadPart = 0;

    DeviceExtension->CursorVa = MmAllocateContiguousMemorySpecifyCache(
        Bytes, Low, High, Boundary, MmWriteCombined);
    if (DeviceExtension->CursorVa == NULL)
    {
        DbgPrint("RPI5VC4: cursor buffer alloc failed\n");
        DeviceExtension->CursorVisible = FALSE;
        return;
    }
    DeviceExtension->CursorPhys = MmGetPhysicalAddress(DeviceExtension->CursorVa);
    DeviceExtension->CursorWidth = RPI5VC4_CURSOR_WIDTH;
    DeviceExtension->CursorHeight = RPI5VC4_CURSOR_HEIGHT;
    DeviceExtension->CursorVisible = FALSE;
    DeviceExtension->CursorShapeValid = FALSE;
    VideoPortZeroMemory(DeviceExtension->CursorVa, Bytes);
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
}

static VP_STATUS
Rpi5Vc4SetPointerAttributes(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(AttributesSize) PVIDEO_POINTER_ATTRIBUTES Attributes,
    _In_ ULONG AttributesSize)
{
    PUCHAR Source;
    PUCHAR Destination;
    ULONG RequiredSize;
    ULONG Row;
    ULONG CopyBytes;

    if (DeviceExtension->CursorVa == NULL ||
        AttributesSize < FIELD_OFFSET(VIDEO_POINTER_ATTRIBUTES, Pixels) ||
        !(Attributes->Flags & VIDEO_MODE_COLOR_POINTER) ||
        Attributes->Width == 0 ||
        Attributes->Height == 0 ||
        Attributes->Width > RPI5VC4_CURSOR_WIDTH ||
        Attributes->Height > RPI5VC4_CURSOR_HEIGHT ||
        Attributes->WidthInBytes < Attributes->Width * sizeof(ULONG))
    {
        return ERROR_INVALID_PARAMETER;
    }

    if (Attributes->Height >
        (MAXULONG - FIELD_OFFSET(VIDEO_POINTER_ATTRIBUTES, Pixels)) /
        Attributes->WidthInBytes)
    {
        return ERROR_INVALID_PARAMETER;
    }

    RequiredSize = FIELD_OFFSET(VIDEO_POINTER_ATTRIBUTES, Pixels) +
                   Attributes->WidthInBytes * Attributes->Height;
    if (AttributesSize < RequiredSize)
        return ERROR_INSUFFICIENT_BUFFER;

    VideoPortZeroMemory(DeviceExtension->CursorVa,
                        RPI5VC4_CURSOR_WIDTH * RPI5VC4_CURSOR_HEIGHT * sizeof(ULONG));

    Source = Attributes->Pixels;
    Destination = DeviceExtension->CursorVa;
    CopyBytes = Attributes->Width * sizeof(ULONG);

    for (Row = 0; Row < Attributes->Height; ++Row)
    {
        VideoPortMoveMemory(Destination + (Row * RPI5VC4_CURSOR_WIDTH * sizeof(ULONG)),
                            Source + (Row * Attributes->WidthInBytes),
                            CopyBytes);
    }

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif

    DeviceExtension->CursorWidth = Attributes->Width;
    DeviceExtension->CursorHeight = Attributes->Height;
    DeviceExtension->CursorX = Attributes->Column;
    DeviceExtension->CursorY = Attributes->Row;
    DeviceExtension->CursorShapeValid = TRUE;
    DeviceExtension->CursorVisible = (Attributes->Enable != 0);

    Rpi5HvsInstallScanout(DeviceExtension);
    return NO_ERROR;
}

static VP_STATUS
Rpi5Vc4SetPointerPosition(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ SHORT Column,
    _In_ SHORT Row)
{
    DeviceExtension->CursorX = Column;
    DeviceExtension->CursorY = Row;
    if (DeviceExtension->CursorShapeValid && DeviceExtension->CursorVisible)
    {
        if (Rpi5HvsMoveCursor(DeviceExtension))
            return NO_ERROR;

        Rpi5HvsInstallScanout(DeviceExtension);
    }

    return NO_ERROR;
}

static VP_STATUS
Rpi5Vc4SetPointerEnabled(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Enable)
{
    DeviceExtension->CursorVisible = Enable && DeviceExtension->CursorShapeValid;
    Rpi5HvsInstallScanout(DeviceExtension);
    return NO_ERROR;
}

BOOLEAN
NTAPI
Rpi5Vc4Initialize(
    _In_ PVOID HwDeviceExtension)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    WCHAR ChipType[] = L"Raspberry Pi 5 firmware framebuffer";
    ULONG SizeInBytes = DeviceExtension->FrameBufferSize;

    Rpi5Vc4BuildModeInfo(DeviceExtension);
    Rpi5Vc4InitFrameBuffer(DeviceExtension);
    Rpi5Vc4InitCursor(DeviceExtension);
    if (Rpi5CrtcReportTiming(DeviceExtension))
        Rpi5CrtcProgramCurrentTiming(DeviceExtension);

    /*
     * Take ownership of the HVS scanout: install our own display list (built
     * to ignore the per-pixel source alpha, i.e. XRGB) over the firmware's at
     * the live head. Without this the HVS treats the top byte as alpha, so
     * GDI-drawn pixels (which leave it 0) are composited to black.
     */
    Rpi5HvsInstallScanout(DeviceExtension);

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

BOOLEAN
NTAPI
Rpi5Vc4StartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension =
        (PRPI5VC4_DEVICE_EXTENSION)HwDeviceExtension;
    VP_STATUS Status = ERROR_INVALID_FUNCTION;
    PVIDEO_MODE_INFORMATION ModeInfo;
    PVIDEO_MEMORY VideoMemory;
    PVIDEO_MEMORY_INFORMATION MemoryInfo;
    PVIDEO_MODE VideoMode;
    PVIDEO_NUM_MODES NumModes;
    PVIDEO_POINTER_CAPABILITIES PointerCaps;
    PVIDEO_POINTER_ATTRIBUTES PointerAttributes;
    PVIDEO_POINTER_POSITION PointerPosition;
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
            if (VideoMode->RequestedMode != 0)
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
            InIoSpace = VIDEO_MEMORY_SPACE_MEMORY | VIDEO_MEMORY_SPACE_P6CACHE;
            MemoryInfo->VideoRamLength = DeviceExtension->FrameBufferSize;
            if (DeviceExtension->FrameBufferVirtual != NULL)
            {
                MemoryInfo->VideoRamBase = DeviceExtension->FrameBufferVirtual;
                Status = NO_ERROR;
            }
            else
            {
                MemoryInfo->VideoRamBase = VideoMemory->RequestedVirtualAddress;
                Status = VideoPortMapMemory(DeviceExtension,
                                            FrameBuffer,
                                            &MemoryInfo->VideoRamLength,
                                            &InIoSpace,
                                            &MemoryInfo->VideoRamBase);
            }
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
            if (DeviceExtension->FrameBufferVirtual != NULL)
                Status = NO_ERROR;
            else
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
            if (DeviceExtension->CursorVa != NULL)
            {
                PointerCaps->Flags = VIDEO_MODE_ASYNC_POINTER |
                                     VIDEO_MODE_COLOR_POINTER;
                PointerCaps->MaxWidth = RPI5VC4_CURSOR_WIDTH;
                PointerCaps->MaxHeight = RPI5VC4_CURSOR_HEIGHT;
            }
            RequestPacket->StatusBlock->Information =
                sizeof(VIDEO_POINTER_CAPABILITIES);
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_SET_POINTER_ATTR:
            if (RequestPacket->InputBufferLength <
                FIELD_OFFSET(VIDEO_POINTER_ATTRIBUTES, Pixels))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            PointerAttributes = (PVIDEO_POINTER_ATTRIBUTES)RequestPacket->InputBuffer;
            Status = Rpi5Vc4SetPointerAttributes(DeviceExtension,
                                                 PointerAttributes,
                                                 RequestPacket->InputBufferLength);
            break;

        case IOCTL_VIDEO_SET_POINTER_POSITION:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_POINTER_POSITION))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            PointerPosition = (PVIDEO_POINTER_POSITION)RequestPacket->InputBuffer;
            Status = Rpi5Vc4SetPointerPosition(DeviceExtension,
                                               PointerPosition->Column,
                                               PointerPosition->Row);
            break;

        case IOCTL_VIDEO_ENABLE_POINTER:
            Status = Rpi5Vc4SetPointerEnabled(DeviceExtension, TRUE);
            break;

        case IOCTL_VIDEO_DISABLE_POINTER:
            Status = Rpi5Vc4SetPointerEnabled(DeviceExtension, FALSE);
            break;

        case IOCTL_VIDEO_RPI5VC4_LATCH_SCANOUT:
            Status = Rpi5HvsFlipScanout(DeviceExtension,
                                        DeviceExtension->FrameBufferPhysical) ?
                     NO_ERROR : ERROR_INVALID_PARAMETER;
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
Rpi5Vc4ResetHw(
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
Rpi5Vc4GetPowerState(
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
Rpi5Vc4SetPowerState(
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
