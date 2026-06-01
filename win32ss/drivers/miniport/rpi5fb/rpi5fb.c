/*
 * PROJECT:     ReactOS Raspberry Pi 5 framebuffer miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RPi5-owned videoport miniport for the firmware GOP framebuffer.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5fb.h"

#define RPI5FB_ACPI_SIGNATURE(a, b, c, d) \
    ((ULONG)(a) | ((ULONG)(b) << 8) | ((ULONG)(c) << 16) | ((ULONG)(d) << 24))

#define RPI5FB_ACPI_XSDT RPI5FB_ACPI_SIGNATURE('X', 'S', 'D', 'T')
#define RPI5FB_ACPI_RSDT RPI5FB_ACPI_SIGNATURE('R', 'S', 'D', 'T')
#define RPI5FB_ACPI_FADT RPI5FB_ACPI_SIGNATURE('F', 'A', 'C', 'P')

static const CHAR Rpi5FbRsdpSignature[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
static const CHAR Rpi5FbOemId[6] = {'R', 'P', 'I', 'F', 'D', 'N'};
static const CHAR Rpi5FbOemTableId[4] = {'R', 'P', 'I', '5'};

#include <pshpack1.h>
typedef struct _RPI5FB_ACPI_HEADER
{
    ULONG Signature;
    ULONG Length;
    UCHAR Revision;
    UCHAR Checksum;
    CHAR OemId[6];
    CHAR OemTableId[8];
    ULONG OemRevision;
    ULONG CreatorId;
    ULONG CreatorRevision;
} RPI5FB_ACPI_HEADER, *PRPI5FB_ACPI_HEADER;

typedef struct _RPI5FB_RSDP
{
    CHAR Signature[8];
    UCHAR Checksum;
    CHAR OemId[6];
    UCHAR Revision;
    ULONG RsdtAddress;
    ULONG Length;
    ULONGLONG XsdtAddress;
    UCHAR ExtendedChecksum;
    UCHAR Reserved[3];
} RPI5FB_RSDP, *PRPI5FB_RSDP;
#include <poppack.h>

typedef struct _RPI5FB_MAPPING
{
    PVOID Base;
    SIZE_T Length;
    PVOID Address;
} RPI5FB_MAPPING, *PRPI5FB_MAPPING;

static BOOLEAN
Rpi5FbMapPhysical(
    _In_ ULONGLONG PhysicalAddress,
    _In_ SIZE_T Length,
    _Out_ PRPI5FB_MAPPING Mapping)
{
    PHYSICAL_ADDRESS AlignedAddress;
    SIZE_T Offset;

    RtlZeroMemory(Mapping, sizeof(*Mapping));

    if (PhysicalAddress == 0 || Length == 0)
        return FALSE;

    Offset = (SIZE_T)(PhysicalAddress & (PAGE_SIZE - 1));
    AlignedAddress.QuadPart = PhysicalAddress - Offset;
    Mapping->Length = ALIGN_UP_BY(Offset + Length, PAGE_SIZE);
    Mapping->Base = MmMapIoSpace(AlignedAddress, Mapping->Length, MmNonCached);
    if (Mapping->Base == NULL)
        return FALSE;

    Mapping->Address = (PUCHAR)Mapping->Base + Offset;
    return TRUE;
}

static VOID
Rpi5FbUnmapPhysical(
    _Inout_ PRPI5FB_MAPPING Mapping)
{
    if (Mapping->Base != NULL)
        MmUnmapIoSpace(Mapping->Base, Mapping->Length);

    RtlZeroMemory(Mapping, sizeof(*Mapping));
}

static BOOLEAN
Rpi5FbChecksumValid(
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Bytes = Buffer;
    UCHAR Sum = 0;
    ULONG i;

    for (i = 0; i < Length; ++i)
        Sum += Bytes[i];

    return Sum == 0;
}

static BOOLEAN
Rpi5FbIsRpi5Fadt(
    _In_ ULONGLONG PhysicalAddress)
{
    RPI5FB_MAPPING HeaderMapping;
    RPI5FB_MAPPING TableMapping;
    PRPI5FB_ACPI_HEADER Header;
    ULONG Length;
    BOOLEAN Match;

    if (!Rpi5FbMapPhysical(PhysicalAddress, sizeof(*Header), &HeaderMapping))
        return FALSE;

    Header = (PRPI5FB_ACPI_HEADER)HeaderMapping.Address;
    if (Header->Signature != RPI5FB_ACPI_FADT ||
        Header->Length < sizeof(*Header))
    {
        Rpi5FbUnmapPhysical(&HeaderMapping);
        return FALSE;
    }

    Length = Header->Length;
    Rpi5FbUnmapPhysical(&HeaderMapping);

    if (!Rpi5FbMapPhysical(PhysicalAddress, Length, &TableMapping))
        return FALSE;

    Header = (PRPI5FB_ACPI_HEADER)TableMapping.Address;
    Match = Header->Signature == RPI5FB_ACPI_FADT &&
            Rpi5FbChecksumValid(Header, Length) &&
            RtlCompareMemory(Header->OemId, Rpi5FbOemId, sizeof(Rpi5FbOemId)) == sizeof(Rpi5FbOemId) &&
            RtlCompareMemory(Header->OemTableId, Rpi5FbOemTableId, sizeof(Rpi5FbOemTableId)) == sizeof(Rpi5FbOemTableId);

    Rpi5FbUnmapPhysical(&TableMapping);
    return Match;
}

static BOOLEAN
Rpi5FbFindFadtInXsdt(
    _In_ ULONGLONG XsdtPhysical)
{
    RPI5FB_MAPPING HeaderMapping;
    RPI5FB_MAPPING TableMapping;
    PRPI5FB_ACPI_HEADER Header;
    PULONGLONG Entries;
    ULONG TableLength;
    ULONG Count;
    ULONG i;
    BOOLEAN Found = FALSE;

    if (!Rpi5FbMapPhysical(XsdtPhysical, sizeof(*Header), &HeaderMapping))
        return FALSE;

    Header = (PRPI5FB_ACPI_HEADER)HeaderMapping.Address;
    if (Header->Signature != RPI5FB_ACPI_XSDT || Header->Length < sizeof(*Header))
    {
        Rpi5FbUnmapPhysical(&HeaderMapping);
        return FALSE;
    }

    TableLength = Header->Length;
    Count = (TableLength - sizeof(*Header)) / sizeof(ULONGLONG);
    Rpi5FbUnmapPhysical(&HeaderMapping);

    if (!Rpi5FbMapPhysical(XsdtPhysical, TableLength, &TableMapping))
        return FALSE;

    Header = (PRPI5FB_ACPI_HEADER)TableMapping.Address;
    if (!Rpi5FbChecksumValid(Header, TableLength))
    {
        Rpi5FbUnmapPhysical(&TableMapping);
        return FALSE;
    }

    Entries = (PULONGLONG)(Header + 1);
    for (i = 0; i < Count; ++i)
    {
        if (Rpi5FbIsRpi5Fadt(Entries[i]))
        {
            Found = TRUE;
            break;
        }
    }

    Rpi5FbUnmapPhysical(&TableMapping);
    return Found;
}

static BOOLEAN
Rpi5FbFindFadtInRsdt(
    _In_ ULONG RsdtPhysical)
{
    RPI5FB_MAPPING HeaderMapping;
    RPI5FB_MAPPING TableMapping;
    PRPI5FB_ACPI_HEADER Header;
    PULONG Entries;
    ULONG TableLength;
    ULONG Count;
    ULONG i;
    BOOLEAN Found = FALSE;

    if (!Rpi5FbMapPhysical(RsdtPhysical, sizeof(*Header), &HeaderMapping))
        return FALSE;

    Header = (PRPI5FB_ACPI_HEADER)HeaderMapping.Address;
    if (Header->Signature != RPI5FB_ACPI_RSDT || Header->Length < sizeof(*Header))
    {
        Rpi5FbUnmapPhysical(&HeaderMapping);
        return FALSE;
    }

    TableLength = Header->Length;
    Count = (TableLength - sizeof(*Header)) / sizeof(ULONG);
    Rpi5FbUnmapPhysical(&HeaderMapping);

    if (!Rpi5FbMapPhysical(RsdtPhysical, TableLength, &TableMapping))
        return FALSE;

    Header = (PRPI5FB_ACPI_HEADER)TableMapping.Address;
    if (!Rpi5FbChecksumValid(Header, TableLength))
    {
        Rpi5FbUnmapPhysical(&TableMapping);
        return FALSE;
    }

    Entries = (PULONG)(Header + 1);
    for (i = 0; i < Count; ++i)
    {
        if (Rpi5FbIsRpi5Fadt(Entries[i]))
        {
            Found = TRUE;
            break;
        }
    }

    Rpi5FbUnmapPhysical(&TableMapping);
    return Found;
}

static BOOLEAN
Rpi5FbIsRpi5Platform(VOID)
{
    HAL_ACPI_ROOT_POINTER_INFORMATION RootPointer;
    RPI5FB_MAPPING Mapping;
    PRPI5FB_RSDP Rsdp;
    ULONG ReturnedLength;
    NTSTATUS Status;
    BOOLEAN Match;

    RtlZeroMemory(&RootPointer, sizeof(RootPointer));
    Status = HalQuerySystemInformation(HalAcpiAuditInformation,
                                       sizeof(RootPointer),
                                       &RootPointer,
                                       &ReturnedLength);
    if (!NT_SUCCESS(Status) || RootPointer.RsdpPhysicalAddress.QuadPart == 0)
        return FALSE;

    if (!Rpi5FbMapPhysical(RootPointer.RsdpPhysicalAddress.QuadPart,
                           sizeof(*Rsdp),
                           &Mapping))
    {
        return FALSE;
    }

    Rsdp = (PRPI5FB_RSDP)Mapping.Address;
    if (RtlCompareMemory(Rsdp->Signature, Rpi5FbRsdpSignature, sizeof(Rpi5FbRsdpSignature)) != sizeof(Rpi5FbRsdpSignature))
    {
        Rpi5FbUnmapPhysical(&Mapping);
        return FALSE;
    }

    if (!Rpi5FbChecksumValid(Rsdp, FIELD_OFFSET(RPI5FB_RSDP, Length)) ||
        (Rsdp->Revision >= 2 && !Rpi5FbChecksumValid(Rsdp, sizeof(*Rsdp))))
    {
        Rpi5FbUnmapPhysical(&Mapping);
        return FALSE;
    }

    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0)
        Match = Rpi5FbFindFadtInXsdt(Rsdp->XsdtAddress);
    else
        Match = Rpi5FbFindFadtInRsdt(Rsdp->RsdtAddress);

    Rpi5FbUnmapPhysical(&Mapping);
    return Match;
}

static VP_STATUS
Rpi5FbLoadGopInfo(
    _Inout_ PRPI5FB_DEVICE_EXTENSION DeviceExtension)
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

    DeviceExtension->FrameBufferPhysical = FbInfo.FrameBufferBase;
    DeviceExtension->FrameBufferSize = (ULONG)VisibleFrameBufferSize;
    DeviceExtension->ScreenWidth = FbInfo.HorizontalResolution;
    DeviceExtension->ScreenHeight = FbInfo.VerticalResolution;
    DeviceExtension->PixelsPerScanLine = FbInfo.PixelsPerScanLine;
    DeviceExtension->BitsPerPixel = FbInfo.PixelFormat;
    DeviceExtension->RedMask = FbInfo.RedMask;
    DeviceExtension->GreenMask = FbInfo.GreenMask;
    DeviceExtension->BlueMask = FbInfo.BlueMask;
    DeviceExtension->BytesPerScanLine =
        DeviceExtension->PixelsPerScanLine * BytesPerPixel;
    DeviceExtension->MappedFrameBuffer = NULL;
    DeviceExtension->CurrentMode = 0;

    return NO_ERROR;
}

static VOID
Rpi5FbBuildModeInfo(
    _Inout_ PRPI5FB_DEVICE_EXTENSION DeviceExtension)
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

    if (!Rpi5FbIsRpi5Platform())
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
    InitData.HwFindAdapter = Rpi5FbFindAdapter;
    InitData.HwInitialize = Rpi5FbInitialize;
    InitData.HwStartIO = Rpi5FbStartIO;
    InitData.HwResetHw = Rpi5FbResetHw;
    InitData.HwGetPowerState = Rpi5FbGetPowerState;
    InitData.HwSetPowerState = Rpi5FbSetPowerState;
    InitData.HwGetVideoChildDescriptor = NULL;
    InitData.HwDeviceExtensionSize = sizeof(RPI5FB_DEVICE_EXTENSION);

    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}

VP_STATUS
NTAPI
Rpi5FbFindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again)
{
    PRPI5FB_DEVICE_EXTENSION DeviceExtension =
        (PRPI5FB_DEVICE_EXTENSION)HwDeviceExtension;
    VP_STATUS Status;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;
    VideoPortZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    if (ConfigInfo->Length < sizeof(VIDEO_PORT_CONFIG_INFO))
        return ERROR_INVALID_PARAMETER;

    Status = Rpi5FbLoadGopInfo(DeviceExtension);
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

BOOLEAN
NTAPI
Rpi5FbInitialize(
    _In_ PVOID HwDeviceExtension)
{
    PRPI5FB_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    WCHAR ChipType[] = L"Raspberry Pi 5 firmware framebuffer";
    ULONG SizeInBytes = DeviceExtension->FrameBufferSize;

    Rpi5FbBuildModeInfo(DeviceExtension);

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
Rpi5FbStartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket)
{
    PRPI5FB_DEVICE_EXTENSION DeviceExtension =
        (PRPI5FB_DEVICE_EXTENSION)HwDeviceExtension;
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
Rpi5FbResetHw(
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
Rpi5FbGetPowerState(
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
Rpi5FbSetPowerState(
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
