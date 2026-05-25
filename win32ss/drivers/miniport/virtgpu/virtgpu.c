/*
 * PROJECT:     ReactOS VirtIO GPU Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal 2D VirtIO GPU videoport miniport for QEMU virtio-gpu
 */

#include "virtgpu.h"

int virtioDebugLevel = 0;
int bDebugPrint = 0;

static VOID
VirtGpuVirtioDebugPrint(const char* Format, ...)
{
    UNREFERENCED_PARAMETER(Format);
}

void (*VirtioDebugPrintProc)(const char* Format, ...) = VirtGpuVirtioDebugPrint;

static u8
VirtGpuReadByte(ULONG_PTR Register)
{
    if (Register > 0xFFFF)
        return VideoPortReadRegisterUchar((PUCHAR)Register);

    return VideoPortReadPortUchar((PUCHAR)Register);
}

static u16
VirtGpuReadWord(ULONG_PTR Register)
{
    if (Register > 0xFFFF)
        return VideoPortReadRegisterUshort((PUSHORT)Register);

    return VideoPortReadPortUshort((PUSHORT)Register);
}

static u32
VirtGpuReadDword(ULONG_PTR Register)
{
    if (Register > 0xFFFF)
        return VideoPortReadRegisterUlong((PULONG)Register);

    return VideoPortReadPortUlong((PULONG)Register);
}

static VOID
VirtGpuWriteByte(ULONG_PTR Register, u8 Value)
{
    if (Register > 0xFFFF)
        VideoPortWriteRegisterUchar((PUCHAR)Register, Value);
    else
        VideoPortWritePortUchar((PUCHAR)Register, Value);
}

static VOID
VirtGpuWriteWord(ULONG_PTR Register, u16 Value)
{
    if (Register > 0xFFFF)
        VideoPortWriteRegisterUshort((PUSHORT)Register, Value);
    else
        VideoPortWritePortUshort((PUSHORT)Register, Value);
}

static VOID
VirtGpuWriteDword(ULONG_PTR Register, u32 Value)
{
    if (Register > 0xFFFF)
        VideoPortWriteRegisterUlong((PULONG)Register, Value);
    else
        VideoPortWritePortUlong((PULONG)Register, Value);
}

static PVOID
VirtGpuAllocateContiguous(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ SIZE_T Size,
    _Out_opt_ PPHYSICAL_ADDRESS PhysicalAddress)
{
    PHYSICAL_ADDRESS Highest;
    PVOID VirtualAddress;

    Highest.QuadPart = MAXLONGLONG;
    Size = ROUND_TO_PAGES(Size);

    VirtualAddress = MmAllocateContiguousMemory(Size, Highest);
    if (VirtualAddress == NULL)
        return NULL;

    VideoPortZeroMemory(VirtualAddress, Size);

    if (PhysicalAddress != NULL)
        *PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);

    UNREFERENCED_PARAMETER(DeviceExtension);
    return VirtualAddress;
}

static PVOID
VirtGpuMemAllocContiguousPages(PVOID Context, size_t Size)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = Context;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID VirtualAddress;
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_MAX_SHARED_ALLOCATIONS; ++Index)
    {
        if (DeviceExtension->SharedAllocations[Index].VirtualAddress == NULL)
            break;
    }

    if (Index == VIRTGPU_MAX_SHARED_ALLOCATIONS)
        return NULL;

    VirtualAddress = VirtGpuAllocateContiguous(DeviceExtension,
                                               Size,
                                               &PhysicalAddress);
    if (VirtualAddress == NULL)
        return NULL;

    DeviceExtension->SharedAllocations[Index].VirtualAddress = VirtualAddress;
    DeviceExtension->SharedAllocations[Index].Length = ROUND_TO_PAGES(Size);
    DeviceExtension->SharedAllocations[Index].PhysicalAddress = PhysicalAddress;

    return VirtualAddress;
}

static VOID
VirtGpuMemFreeContiguousPages(PVOID Context, PVOID VirtualAddress)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = Context;
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_MAX_SHARED_ALLOCATIONS; ++Index)
    {
        if (DeviceExtension->SharedAllocations[Index].VirtualAddress == VirtualAddress)
        {
            MmFreeContiguousMemory(VirtualAddress);
            VideoPortZeroMemory(&DeviceExtension->SharedAllocations[Index],
                                sizeof(DeviceExtension->SharedAllocations[Index]));
            return;
        }
    }
}

static ULONGLONG
VirtGpuMemGetPhysicalAddress(PVOID Context, PVOID VirtualAddress)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = Context;
    ULONG_PTR Address = (ULONG_PTR)VirtualAddress;
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_MAX_SHARED_ALLOCATIONS; ++Index)
    {
        ULONG_PTR Base = (ULONG_PTR)DeviceExtension->SharedAllocations[Index].VirtualAddress;
        ULONG Length = DeviceExtension->SharedAllocations[Index].Length;

        if ((Base != 0) && (Address >= Base) && (Address < Base + Length))
        {
            return DeviceExtension->SharedAllocations[Index].PhysicalAddress.QuadPart +
                   (Address - Base);
        }
    }

    return MmGetPhysicalAddress(VirtualAddress).QuadPart;
}

static PVOID
VirtGpuMemAllocNonPagedBlock(PVOID Context, size_t Size)
{
    PVOID Buffer;

    Buffer = VideoPortAllocatePool(Context, VpNonPagedPool, Size, VIRTGPU_TAG);
    if (Buffer != NULL)
        VideoPortZeroMemory(Buffer, Size);

    return Buffer;
}

static VOID
VirtGpuMemFreeNonPagedBlock(PVOID Context, PVOID Address)
{
    if (Address != NULL)
        VideoPortFreePool(Context, Address);
}

static int
VirtGpuPciReadConfig(PVOID Context, int Offset, PVOID Buffer, size_t Length)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = Context;
    ULONG Read;

    Read = HalGetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciBusNumber,
                                 DeviceExtension->PciSlot,
                                 Buffer,
                                 Offset,
                                 (ULONG)Length);

    return (Read == Length) ? 0 : -1;
}

static int
VirtGpuPciReadConfigByte(PVOID Context, int Offset, u8* Value)
{
    return VirtGpuPciReadConfig(Context, Offset, Value, sizeof(*Value));
}

static int
VirtGpuPciReadConfigWord(PVOID Context, int Offset, u16* Value)
{
    return VirtGpuPciReadConfig(Context, Offset, Value, sizeof(*Value));
}

static int
VirtGpuPciReadConfigDword(PVOID Context, int Offset, u32* Value)
{
    return VirtGpuPciReadConfig(Context, Offset, Value, sizeof(*Value));
}

static size_t
VirtGpuPciGetResourceLength(PVOID Context, int Bar)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = Context;

    if ((Bar < 0) || (Bar >= PCI_TYPE0_ADDRESSES))
        return 0;

    return DeviceExtension->Bars[Bar].Length;
}

static PVOID
VirtGpuPciMapAddressRange(PVOID Context, int Bar, size_t Offset, size_t Length)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = Context;
    PHYSICAL_ADDRESS Base;
    ULONG AddressSpace;

    if ((Bar < 0) || (Bar >= PCI_TYPE0_ADDRESSES) ||
        (DeviceExtension->Bars[Bar].Base.QuadPart == 0) ||
        (Offset >= DeviceExtension->Bars[Bar].Length))
    {
        return NULL;
    }

    if (Length > DeviceExtension->Bars[Bar].Length - Offset)
        Length = DeviceExtension->Bars[Bar].Length - Offset;

    if (DeviceExtension->Bars[Bar].InIoSpace)
        return (PUCHAR)(ULONG_PTR)DeviceExtension->Bars[Bar].Base.LowPart + Offset;

    Base.QuadPart = DeviceExtension->Bars[Bar].Base.QuadPart + Offset;
    AddressSpace = VIDEO_MEMORY_SPACE_MEMORY;
    return VideoPortGetDeviceBase(DeviceExtension, Base, (ULONG)Length, AddressSpace);
}

static u16
VirtGpuGetMsixVector(PVOID Context, int Queue)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Queue);
    return VIRTIO_MSI_NO_VECTOR;
}

static VOID
VirtGpuSleep(PVOID Context, unsigned int Milliseconds)
{
    UNREFERENCED_PARAMETER(Context);
    VideoPortStallExecution(Milliseconds * 1000);
}

static const VirtIOSystemOps VirtGpuSystemOps =
{
    VirtGpuReadByte,
    VirtGpuReadWord,
    VirtGpuReadDword,
    VirtGpuWriteByte,
    VirtGpuWriteWord,
    VirtGpuWriteDword,
    VirtGpuMemAllocContiguousPages,
    VirtGpuMemFreeContiguousPages,
    VirtGpuMemGetPhysicalAddress,
    VirtGpuMemAllocNonPagedBlock,
    VirtGpuMemFreeNonPagedBlock,
    VirtGpuPciReadConfigByte,
    VirtGpuPciReadConfigWord,
    VirtGpuPciReadConfigDword,
    VirtGpuPciGetResourceLength,
    VirtGpuPciMapAddressRange,
    VirtGpuGetMsixVector,
    VirtGpuSleep
};

static BOOLEAN
VirtGpuIsSupportedDevice(_In_ PPCI_COMMON_HEADER Header)
{
    if (Header->VendorID != VIRTGPU_VENDOR_ID)
        return FALSE;

    if (Header->DeviceID == VIRTGPU_MODERN_DEVICE_ID)
        return TRUE;

    return (Header->DeviceID >= VIRTGPU_LEGACY_DEVICE_ID_BASE) &&
           (Header->DeviceID <= VIRTGPU_LEGACY_DEVICE_ID_LAST) &&
           (Header->u.type0.SubSystemID == VIRTGPU_DEVICE_ID);
}

static VOID
VirtGpuCaptureBars(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Bar;

    VideoPortZeroMemory(DeviceExtension->Bars, sizeof(DeviceExtension->Bars));

    for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES; ++Bar)
    {
        ULONG RawBar = DeviceExtension->PciHeader.u.type0.BaseAddresses[Bar];

        if (RawBar == 0)
            continue;

        if (RawBar & PCI_ADDRESS_IO_SPACE)
        {
            DeviceExtension->Bars[Bar].Base.QuadPart = RawBar & PCI_ADDRESS_IO_ADDRESS_MASK;
            DeviceExtension->Bars[Bar].InIoSpace = TRUE;
            DeviceExtension->Bars[Bar].Length = 0x1000;
        }
        else
        {
            DeviceExtension->Bars[Bar].Base.LowPart = RawBar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
            DeviceExtension->Bars[Bar].Base.HighPart = 0;
            DeviceExtension->Bars[Bar].InIoSpace = FALSE;
            DeviceExtension->Bars[Bar].Length = 0x100000;

            if ((RawBar & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
            {
                if (Bar + 1 < PCI_TYPE0_ADDRESSES)
                {
                    DeviceExtension->Bars[Bar].Base.HighPart =
                        DeviceExtension->PciHeader.u.type0.BaseAddresses[Bar + 1];
                }
                ++Bar;
            }
        }
    }
}

static VOID
VirtGpuApplyAccessRanges(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_(AccessRangeCount) PVIDEO_ACCESS_RANGE AccessRanges,
    _In_ ULONG AccessRangeCount)
{
    ULONG RangeIndex;

    for (RangeIndex = 0; RangeIndex < AccessRangeCount; ++RangeIndex)
    {
        PVIDEO_ACCESS_RANGE Range = &AccessRanges[RangeIndex];
        ULONG Bar;

        if (Range->RangeLength == 0)
            continue;

        for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES; ++Bar)
        {
            if ((DeviceExtension->Bars[Bar].Base.QuadPart == Range->RangeStart.QuadPart) &&
                (DeviceExtension->Bars[Bar].InIoSpace == (Range->RangeInIoSpace != 0)))
            {
                DeviceExtension->Bars[Bar].Length = Range->RangeLength;
                break;
            }
        }
    }
}

static BOOLEAN
VirtGpuFindPciDevice(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PVIDEO_PORT_CONFIG_INFO ConfigInfo)
{
    VIDEO_ACCESS_RANGE AccessRanges[VIRTGPU_MAX_ACCESS_RANGES];
    PCI_SLOT_NUMBER Slot;
    PCI_COMMON_HEADER Header;
    USHORT VendorId;
    USHORT DeviceId;
    ULONG Device;
    ULONG Function;
    ULONG Read;
    VP_STATUS Status;
    BOOLEAN MultiFunction;

    VendorId = VIRTGPU_VENDOR_ID;
    DeviceId = VIRTGPU_MODERN_DEVICE_ID;
    VideoPortZeroMemory(AccessRanges, sizeof(AccessRanges));

    Status = VideoPortGetAccessRanges(DeviceExtension,
                                      0,
                                      NULL,
                                      VIRTGPU_MAX_ACCESS_RANGES,
                                      AccessRanges,
                                      &VendorId,
                                      &DeviceId,
                                      &DeviceExtension->PciSlot);
    if (Status == NO_ERROR)
    {
        Read = VideoPortGetBusData(DeviceExtension,
                                   PCIConfiguration,
                                   DeviceExtension->PciSlot,
                                   &Header,
                                   0,
                                   sizeof(Header));
        if ((Read >= PCI_COMMON_HDR_LENGTH) && VirtGpuIsSupportedDevice(&Header))
        {
            DeviceExtension->PciBusNumber = ConfigInfo->SystemIoBusNumber;
            VideoPortMoveMemory(&DeviceExtension->PciHeader, &Header, sizeof(Header));
            VirtGpuCaptureBars(DeviceExtension);
            VirtGpuApplyAccessRanges(DeviceExtension,
                                     AccessRanges,
                                     VIRTGPU_MAX_ACCESS_RANGES);
            return TRUE;
        }
    }

    for (Device = 0; Device < PCI_MAX_DEVICES; ++Device)
    {
        MultiFunction = TRUE;

        for (Function = 0; Function < PCI_MAX_FUNCTION; ++Function)
        {
            Slot.u.AsULONG = 0;
            Slot.u.bits.DeviceNumber = Device;
            Slot.u.bits.FunctionNumber = Function;

            Read = HalGetBusDataByOffset(PCIConfiguration,
                                         ConfigInfo->SystemIoBusNumber,
                                         Slot.u.AsULONG,
                                         &Header,
                                         0,
                                         sizeof(Header));
            if ((Read < PCI_COMMON_HDR_LENGTH) ||
                (Header.VendorID == PCI_INVALID_VENDORID) ||
                (Header.VendorID == 0))
            {
                if (Function == 0)
                    break;
                continue;
            }

            if (Function == 0)
                MultiFunction = ((Header.HeaderType & PCI_MULTIFUNCTION) != 0);

            if (VirtGpuIsSupportedDevice(&Header))
            {
                DeviceExtension->PciBusNumber = ConfigInfo->SystemIoBusNumber;
                DeviceExtension->PciSlot = Slot.u.AsULONG;
                VideoPortMoveMemory(&DeviceExtension->PciHeader, &Header, sizeof(Header));
                VirtGpuCaptureBars(DeviceExtension);
                return TRUE;
            }

            if (!MultiFunction)
                break;
        }
    }

    return FALSE;
}

static VOID
VirtGpuEnablePciDevice(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    USHORT Command;

    Command = DeviceExtension->PciHeader.Command |
              PCI_ENABLE_MEMORY_SPACE |
              PCI_ENABLE_IO_SPACE |
              PCI_ENABLE_BUS_MASTER;

    HalSetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciBusNumber,
                          DeviceExtension->PciSlot,
                          &Command,
                          offsetof(PCI_COMMON_HEADER, Command),
                          sizeof(Command));
}

static PVIRTGPU_CTRL_HDR
VirtGpuResponseBuffer(_In_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    return (PVIRTGPU_CTRL_HDR)((PUCHAR)DeviceExtension->CommandBuffer +
                               VIRTGPU_RESPONSE_OFFSET);
}

static VOID
VirtGpuPrepareFence(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTGPU_CTRL_HDR Header,
    _In_ ULONG RingIndex,
    _Out_opt_ PULONGLONG FenceId)
{
    ULONGLONG Id;

    Id = ++DeviceExtension->NextFenceId;
    if (Id == 0)
        Id = ++DeviceExtension->NextFenceId;

    Header->Flags |= VIRTGPU_FLAG_FENCE;
    Header->FenceId = Id;
    if (DeviceExtension->ContextInitSupported)
    {
        Header->Flags |= VIRTGPU_FLAG_INFO_RING_IDX;
        Header->RingIndex = (UCHAR)RingIndex;
    }

    if (FenceId != NULL)
        *FenceId = Id;
}

static VOID
VirtGpuRecordFenceCompletion(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(ResponseSize) PVOID Response,
    _In_ ULONG ResponseSize)
{
    PVIRTGPU_CTRL_HDR Header;

    if ((Response == NULL) || (ResponseSize < sizeof(*Header)))
        return;

    Header = Response;
    if ((Header->FenceId != 0) &&
        (Header->FenceId > DeviceExtension->CompletedFenceId))
    {
        DeviceExtension->CompletedFenceId = Header->FenceId;
    }
}

static BOOLEAN
VirtGpuGetAsyncCommandSlot(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PVOID Used,
    _Out_ PULONG Slot)
{
    ULONG_PTR Base;
    ULONG_PTR Pointer;
    ULONG Offset;

    if ((DeviceExtension->AsyncCommandBuffer == NULL) || (Used == NULL))
        return FALSE;

    Base = (ULONG_PTR)DeviceExtension->AsyncCommandBuffer;
    Pointer = (ULONG_PTR)Used;
    if ((Pointer < Base) ||
        (Pointer >= Base + VIRTGPU_ASYNC_COMMAND_COUNT * VIRTGPU_ASYNC_COMMAND_SLOT_SIZE))
    {
        return FALSE;
    }

    Offset = (ULONG)(Pointer - Base);
    if ((Offset % VIRTGPU_ASYNC_COMMAND_SLOT_SIZE) != 0)
        return FALSE;

    *Slot = Offset / VIRTGPU_ASYNC_COMMAND_SLOT_SIZE;
    return *Slot < VIRTGPU_ASYNC_COMMAND_COUNT;
}

static BOOLEAN
VirtGpuReapAsyncCommand(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PVOID Used,
    _In_ ULONG Length)
{
    PUCHAR SlotBase;
    ULONG Slot;

    UNREFERENCED_PARAMETER(Length);

    if (!VirtGpuGetAsyncCommandSlot(DeviceExtension, Used, &Slot))
        return FALSE;

    SlotBase = (PUCHAR)DeviceExtension->AsyncCommandBuffer +
               (Slot * VIRTGPU_ASYNC_COMMAND_SLOT_SIZE);
    VirtGpuRecordFenceCompletion(DeviceExtension,
                                 SlotBase + VIRTGPU_ASYNC_RESPONSE_OFFSET,
                                 sizeof(VIRTGPU_CTRL_HDR));
    DeviceExtension->AsyncCommandInUse[Slot] = FALSE;
    return TRUE;
}

static VOID
VirtGpuReapAsyncCommands(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    unsigned int Length;
    PVOID Used;

    if (DeviceExtension->ControlQueue == NULL)
        return;

    while ((Used = virtqueue_get_buf(DeviceExtension->ControlQueue, &Length)) != NULL)
    {
        if (!VirtGpuReapAsyncCommand(DeviceExtension, Used, Length))
            return;
    }
}

static ULONG
VirtGpuAsyncFreeCommandCount(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Count = 0;
    ULONG Index;

    VirtGpuReapAsyncCommands(DeviceExtension);

    for (Index = 0; Index < VIRTGPU_ASYNC_COMMAND_COUNT; ++Index)
    {
        if (!DeviceExtension->AsyncCommandInUse[Index])
            ++Count;
    }

    return Count;
}

static ULONG
VirtGpuFindFreeAsyncCommand(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_ASYNC_COMMAND_COUNT; ++Index)
    {
        if (!DeviceExtension->AsyncCommandInUse[Index])
            return Index;
    }

    return VIRTGPU_ASYNC_COMMAND_COUNT;
}

static BOOLEAN
VirtGpuSendCommandAsync(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(RequestSize) PVOID Request,
    _In_ ULONG RequestSize)
{
    struct scatterlist Sg[2];
    PUCHAR SlotBase;
    ULONG Slot;

    if (!DeviceExtension->ControlQueue ||
        !DeviceExtension->AsyncCommandBuffer ||
        (RequestSize > VIRTGPU_ASYNC_RESPONSE_OFFSET) ||
        DeviceExtension->CommandBusy)
    {
        return FALSE;
    }

    Slot = VirtGpuFindFreeAsyncCommand(DeviceExtension);
    if (Slot >= VIRTGPU_ASYNC_COMMAND_COUNT)
        return FALSE;

    SlotBase = (PUCHAR)DeviceExtension->AsyncCommandBuffer +
               (Slot * VIRTGPU_ASYNC_COMMAND_SLOT_SIZE);
    VideoPortZeroMemory(SlotBase, VIRTGPU_ASYNC_COMMAND_SLOT_SIZE);
    VideoPortMoveMemory(SlotBase, Request, RequestSize);

    Sg[0].physAddr.QuadPart =
        DeviceExtension->AsyncCommandPhysical.QuadPart +
        (Slot * VIRTGPU_ASYNC_COMMAND_SLOT_SIZE);
    Sg[0].length = RequestSize;
    Sg[1].physAddr.QuadPart =
        DeviceExtension->AsyncCommandPhysical.QuadPart +
        (Slot * VIRTGPU_ASYNC_COMMAND_SLOT_SIZE) +
        VIRTGPU_ASYNC_RESPONSE_OFFSET;
    Sg[1].length = sizeof(VIRTGPU_CTRL_HDR);

    if (virtqueue_add_buf(DeviceExtension->ControlQueue,
                          Sg,
                          1,
                          1,
                          SlotBase,
                          NULL,
                          0) != 0)
    {
        return FALSE;
    }

    DeviceExtension->AsyncCommandInUse[Slot] = TRUE;
    virtqueue_kick(DeviceExtension->ControlQueue);
    return TRUE;
}

static BOOLEAN
VirtGpuSendCommand(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(RequestSize) PVOID Request,
    _In_ ULONG RequestSize,
    _In_ ULONG ResponseSize,
    _Outptr_opt_ PVOID* Response)
{
    struct scatterlist Sg[2];
    PUCHAR RequestBuffer;
    PUCHAR ResponseBuffer;
    unsigned int Length;
    ULONG Retry;
    PVOID Used;

    if (Response != NULL)
        *Response = NULL;

    if (!DeviceExtension->ControlQueue ||
        !DeviceExtension->CommandBuffer ||
        (RequestSize > VIRTGPU_RESPONSE_OFFSET) ||
        (ResponseSize > VIRTGPU_COMMAND_BUFFER_SIZE - VIRTGPU_RESPONSE_OFFSET) ||
        DeviceExtension->CommandBusy)
    {
        return FALSE;
    }

    VirtGpuReapAsyncCommands(DeviceExtension);
    DeviceExtension->CommandBusy = TRUE;

    RequestBuffer = DeviceExtension->CommandBuffer;
    ResponseBuffer = (PUCHAR)DeviceExtension->CommandBuffer + VIRTGPU_RESPONSE_OFFSET;
    VideoPortZeroMemory(DeviceExtension->CommandBuffer, VIRTGPU_COMMAND_BUFFER_SIZE);
    VideoPortMoveMemory(RequestBuffer, Request, RequestSize);

    Sg[0].physAddr.QuadPart = DeviceExtension->CommandPhysical.QuadPart;
    Sg[0].length = RequestSize;
    Sg[1].physAddr.QuadPart = DeviceExtension->CommandPhysical.QuadPart +
                              VIRTGPU_RESPONSE_OFFSET;
    Sg[1].length = ResponseSize;

    if (virtqueue_add_buf(DeviceExtension->ControlQueue,
                          Sg,
                          1,
                          1,
                          RequestBuffer,
                          NULL,
                          0) != 0)
    {
        DeviceExtension->CommandBusy = FALSE;
        return FALSE;
    }

    virtqueue_kick(DeviceExtension->ControlQueue);

    for (Retry = 0; Retry < 10000; ++Retry)
    {
        Used = virtqueue_get_buf(DeviceExtension->ControlQueue, &Length);
        if (Used == RequestBuffer)
        {
            VirtGpuRecordFenceCompletion(DeviceExtension,
                                         ResponseBuffer,
                                         ResponseSize);
            if (Response != NULL)
                *Response = ResponseBuffer;
            DeviceExtension->CommandBusy = FALSE;
            return TRUE;
        }

        if (Used != NULL)
            VirtGpuReapAsyncCommand(DeviceExtension, Used, Length);

        VideoPortStallExecution(10);
    }

    DeviceExtension->CommandBusy = FALSE;
    return FALSE;
}

static BOOLEAN
VirtGpuSendCommandWithPayload(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(HeaderSize) PVOID Header,
    _In_ ULONG HeaderSize,
    _In_reads_bytes_opt_(PayloadSize) PVOID Payload,
    _In_ ULONG PayloadSize,
    _In_ ULONG ResponseSize,
    _Outptr_opt_ PVOID* Response)
{
    struct scatterlist Sg[2];
    PUCHAR RequestBuffer;
    PUCHAR ResponseBuffer;
    ULONG RequestSize;
    unsigned int Length;
    ULONG Retry;
    PVOID Used;

    if (Response != NULL)
        *Response = NULL;

    if ((HeaderSize > VIRTGPU_RESPONSE_OFFSET) ||
        (PayloadSize > VIRTGPU_RESPONSE_OFFSET - HeaderSize) ||
        ((PayloadSize != 0) && (Payload == NULL)) ||
        !DeviceExtension->ControlQueue ||
        !DeviceExtension->CommandBuffer ||
        (ResponseSize > VIRTGPU_COMMAND_BUFFER_SIZE - VIRTGPU_RESPONSE_OFFSET) ||
        DeviceExtension->CommandBusy)
    {
        return FALSE;
    }

    VirtGpuReapAsyncCommands(DeviceExtension);
    DeviceExtension->CommandBusy = TRUE;

    RequestSize = HeaderSize + PayloadSize;
    RequestBuffer = DeviceExtension->CommandBuffer;
    ResponseBuffer = (PUCHAR)DeviceExtension->CommandBuffer + VIRTGPU_RESPONSE_OFFSET;
    VideoPortZeroMemory(DeviceExtension->CommandBuffer, VIRTGPU_COMMAND_BUFFER_SIZE);
    VideoPortMoveMemory(RequestBuffer, Header, HeaderSize);
    if (PayloadSize != 0)
        VideoPortMoveMemory(RequestBuffer + HeaderSize, Payload, PayloadSize);

    Sg[0].physAddr.QuadPart = DeviceExtension->CommandPhysical.QuadPart;
    Sg[0].length = RequestSize;
    Sg[1].physAddr.QuadPart = DeviceExtension->CommandPhysical.QuadPart +
                              VIRTGPU_RESPONSE_OFFSET;
    Sg[1].length = ResponseSize;

    if (virtqueue_add_buf(DeviceExtension->ControlQueue,
                          Sg,
                          1,
                          1,
                          RequestBuffer,
                          NULL,
                          0) != 0)
    {
        DeviceExtension->CommandBusy = FALSE;
        return FALSE;
    }

    virtqueue_kick(DeviceExtension->ControlQueue);

    for (Retry = 0; Retry < 10000; ++Retry)
    {
        Used = virtqueue_get_buf(DeviceExtension->ControlQueue, &Length);
        if (Used == RequestBuffer)
        {
            VirtGpuRecordFenceCompletion(DeviceExtension,
                                         ResponseBuffer,
                                         ResponseSize);
            if (Response != NULL)
                *Response = ResponseBuffer;
            DeviceExtension->CommandBusy = FALSE;
            return TRUE;
        }

        if (Used != NULL)
            VirtGpuReapAsyncCommand(DeviceExtension, Used, Length);

        VideoPortStallExecution(10);
    }

    DeviceExtension->CommandBusy = FALSE;
    return FALSE;
}

static BOOLEAN
VirtGpuCommandOk(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(RequestSize) PVOID Request,
    _In_ ULONG RequestSize)
{
    PVIRTGPU_CTRL_HDR Response;

    if (!VirtGpuSendCommand(DeviceExtension,
                            Request,
                            RequestSize,
                            sizeof(*Response),
                            (PVOID*)&Response))
    {
        return FALSE;
    }

    return Response->Type == VIRTGPU_RESP_OK_NODATA;
}

static BOOLEAN
VirtGpuCommandOkWithPayload(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(HeaderSize) PVOID Header,
    _In_ ULONG HeaderSize,
    _In_reads_bytes_opt_(PayloadSize) PVOID Payload,
    _In_ ULONG PayloadSize)
{
    PVIRTGPU_CTRL_HDR Response;

    if (!VirtGpuSendCommandWithPayload(DeviceExtension,
                                       Header,
                                       HeaderSize,
                                       Payload,
                                       PayloadSize,
                                       sizeof(*Response),
                                       (PVOID*)&Response))
    {
        return FALSE;
    }

    return Response->Type == VIRTGPU_RESP_OK_NODATA;
}

static BOOLEAN
VirtGpuGetDisplayInfo(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    VIRTGPU_CTRL_HDR Request;
    PVIRTGPU_RESP_DISPLAY_INFO Response;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Type = VIRTGPU_CMD_GET_DISPLAY_INFO;

    if (!VirtGpuSendCommand(DeviceExtension,
                            &Request,
                            sizeof(Request),
                            sizeof(*Response),
                            (PVOID*)&Response))
    {
        return FALSE;
    }

    if ((Response->Header.Type != VIRTGPU_RESP_OK_DISPLAY_INFO) ||
        (Response->Modes[0].Rect.Width == 0) ||
        (Response->Modes[0].Rect.Height == 0))
    {
        return FALSE;
    }

    *Width = Response->Modes[0].Rect.Width;
    *Height = Response->Modes[0].Rect.Height;
    return TRUE;
}

static BOOLEAN
VirtGpuEdidChecksumOk(
    _In_reads_bytes_(VIRTGPU_SHARED_EDID_SIZE) const UCHAR* Edid,
    _In_ ULONG Size)
{
    static const UCHAR Header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    ULONG Index;
    UCHAR Sum = 0;

    if ((Edid == NULL) || (Size < 128))
        return FALSE;

    for (Index = 0; Index < sizeof(Header); ++Index)
    {
        if (Edid[Index] != Header[Index])
            return FALSE;
    }

    for (Index = 0; Index < 128; ++Index)
        Sum = (UCHAR)(Sum + Edid[Index]);

    return Sum == 0;
}

static BOOLEAN
VirtGpuQueryEdid(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    VIRTGPU_GET_EDID Request;
    PVIRTGPU_RESP_EDID Response;
    ULONG CopySize;

    DeviceExtension->EdidValid = FALSE;
    DeviceExtension->EdidSize = 0;
    VideoPortZeroMemory(DeviceExtension->Edid, sizeof(DeviceExtension->Edid));

    if (!DeviceExtension->EdidSupported)
        return FALSE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_GET_EDID;
    Request.Scanout = VIRTGPU_SCANOUT_ID;

    if (!VirtGpuSendCommand(DeviceExtension,
                            &Request,
                            sizeof(Request),
                            sizeof(*Response),
                            (PVOID*)&Response))
    {
        return FALSE;
    }

    if (Response->Header.Type != VIRTGPU_RESP_OK_EDID)
        return FALSE;

    CopySize = Response->Size;
    if ((CopySize == 0) || (CopySize > VIRTGPU_EDID_SIZE))
        CopySize = VIRTGPU_EDID_SIZE;
    if (CopySize > sizeof(DeviceExtension->Edid))
        CopySize = sizeof(DeviceExtension->Edid);

    VideoPortMoveMemory(DeviceExtension->Edid, Response->Edid, CopySize);
    DeviceExtension->EdidSize = CopySize;
    DeviceExtension->EdidValid =
        VirtGpuEdidChecksumOk(DeviceExtension->Edid, CopySize);

    return DeviceExtension->EdidValid;
}

static ULONG
VirtGpuReadConfigU32(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset)
{
    ULONG Value = 0;

    virtio_get_config(&DeviceExtension->VirtIODevice,
                      Offset,
                      &Value,
                      sizeof(Value));
    return Value;
}

static BOOLEAN
VirtGpuQuery3DCapsets(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;
    ULONG Count;

    DeviceExtension->NumScanouts =
        VirtGpuReadConfigU32(DeviceExtension, offsetof(VIRTGPU_CONFIG, NumScanouts));
    DeviceExtension->NumCapsets =
        VirtGpuReadConfigU32(DeviceExtension, offsetof(VIRTGPU_CONFIG, NumCapsets));
    DeviceExtension->CapsetCount = 0;
    DeviceExtension->SupportedCapsetMask = 0;
    DeviceExtension->PreferredCapsetId = 0;
    DeviceExtension->PreferredCapsetVersion = 0;
    VideoPortZeroMemory(DeviceExtension->Capsets, sizeof(DeviceExtension->Capsets));

    if (!DeviceExtension->VirglSupported || (DeviceExtension->NumCapsets == 0))
        return TRUE;

    Count = DeviceExtension->NumCapsets;
    if (Count > VIRTGPU_MAX_CAPSETS)
        Count = VIRTGPU_MAX_CAPSETS;

    for (Index = 0; Index < Count; ++Index)
    {
        VIRTGPU_GET_CAPSET_INFO Request;
        PVIRTGPU_RESP_CAPSET_INFO Response;

        VideoPortZeroMemory(&Request, sizeof(Request));
        Request.Header.Type = VIRTGPU_CMD_GET_CAPSET_INFO;
        Request.CapsetIndex = Index;

        if (!VirtGpuSendCommand(DeviceExtension,
                                &Request,
                                sizeof(Request),
                                sizeof(*Response),
                                (PVOID*)&Response))
        {
            continue;
        }

        if ((Response->Header.Type != VIRTGPU_RESP_OK_CAPSET_INFO) ||
            (Response->CapsetId == 0) ||
            (Response->CapsetMaxSize == 0))
        {
            continue;
        }

        DeviceExtension->Capsets[DeviceExtension->CapsetCount].CapsetId =
            Response->CapsetId;
        DeviceExtension->Capsets[DeviceExtension->CapsetCount].MaxVersion =
            Response->CapsetMaxVersion;
        DeviceExtension->Capsets[DeviceExtension->CapsetCount].MaxSize =
            Response->CapsetMaxSize;
        if (Response->CapsetId < 32)
            DeviceExtension->SupportedCapsetMask |= (1UL << Response->CapsetId);
        if ((Response->CapsetId == VIRTGPU_CAPSET_VIRGL2) ||
            ((Response->CapsetId == VIRTGPU_CAPSET_VIRGL) &&
             (DeviceExtension->PreferredCapsetId == 0)))
        {
            DeviceExtension->PreferredCapsetId = Response->CapsetId;
            DeviceExtension->PreferredCapsetVersion = Response->CapsetMaxVersion;
        }
        DeviceExtension->CapsetCount++;
    }

    DeviceExtension->VirglSupported =
        DeviceExtension->VirglSupported &&
        (DeviceExtension->PreferredCapsetId != 0);

    return TRUE;
}

static PVIRTGPU_3D_CAPSET_INFO
VirtGpuFindCapset(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG CapsetId)
{
    ULONG Index;

    for (Index = 0; Index < DeviceExtension->CapsetCount; ++Index)
    {
        if (DeviceExtension->Capsets[Index].CapsetId == CapsetId)
            return &DeviceExtension->Capsets[Index];
    }

    return NULL;
}

static BOOLEAN
VirtGpuGetCapset(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG CapsetId,
    _In_ ULONG CapsetVersion,
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize)
{
    VIRTGPU_GET_CAPSET Request;
    PVIRTGPU_RESP_CAPSET Response;

    if (!DeviceExtension->VirglSupported ||
        (Buffer == NULL) ||
        (BufferSize > VIRTGPU_COMMAND_BUFFER_SIZE - VIRTGPU_RESPONSE_OFFSET -
                      sizeof(VIRTGPU_CTRL_HDR)))
    {
        return FALSE;
    }

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_GET_CAPSET;
    Request.CapsetId = CapsetId;
    Request.CapsetVersion = CapsetVersion;

    if (!VirtGpuSendCommand(DeviceExtension,
                            &Request,
                            sizeof(Request),
                            sizeof(VIRTGPU_CTRL_HDR) + BufferSize,
                            (PVOID*)&Response))
    {
        return FALSE;
    }

    if (Response->Header.Type != VIRTGPU_RESP_OK_CAPSET)
        return FALSE;

    VideoPortMoveMemory(Buffer, Response->CapsetData, BufferSize);
    return TRUE;
}

static ULONG
VirtGpuContextNameLength(_In_reads_(VIRTGPU_SHARED_CONTEXT_NAME_SIZE) const CHAR* Name)
{
    ULONG Length;

    for (Length = 0; Length < VIRTGPU_SHARED_CONTEXT_NAME_SIZE; ++Length)
    {
        if (Name[Length] == '\0')
            break;
    }

    return Length;
}

static PVIRTGPU_3D_CONTEXT_STATE
VirtGpuFindContext(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ContextId)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_MAX_3D_CONTEXTS; ++Index)
    {
        if (DeviceExtension->Contexts[Index].InUse &&
            (DeviceExtension->Contexts[Index].ContextId == ContextId))
        {
            return &DeviceExtension->Contexts[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_3D_CONTEXT_STATE
VirtGpuFindFreeContext(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_MAX_3D_CONTEXTS; ++Index)
    {
        if (!DeviceExtension->Contexts[Index].InUse)
            return &DeviceExtension->Contexts[Index];
    }

    return NULL;
}

static ULONG
VirtGpuAllocateContextId(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Attempt;

    for (Attempt = 0; Attempt < VIRTGPU_MAX_3D_CONTEXTS * 4; ++Attempt)
    {
        ULONG ContextId = DeviceExtension->NextContextId++;

        if (DeviceExtension->NextContextId == 0)
            DeviceExtension->NextContextId = 1;

        if ((ContextId != 0) && (VirtGpuFindContext(DeviceExtension, ContextId) == NULL))
            return ContextId;
    }

    return 0;
}

static BOOLEAN
VirtGpuCreateContext(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PULONG ContextId,
    _In_ ULONG CapsetId,
    _In_ ULONG ContextInit,
    _In_reads_(VIRTGPU_SHARED_CONTEXT_NAME_SIZE) const CHAR* DebugName)
{
    PVIRTGPU_3D_CONTEXT_STATE Slot;
    VIRTGPU_CTX_CREATE Request;
    ULONG Id;

    if (!DeviceExtension->VirglSupported || (ContextId == NULL))
        return FALSE;

    Slot = VirtGpuFindFreeContext(DeviceExtension);
    if (Slot == NULL)
        return FALSE;

    Id = *ContextId;
    if (Id == 0)
    {
        Id = VirtGpuAllocateContextId(DeviceExtension);
        if (Id == 0)
            return FALSE;
    }
    else if (VirtGpuFindContext(DeviceExtension, Id) != NULL)
    {
        return FALSE;
    }

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_CTX_CREATE;
    Request.Header.ContextId = Id;
    Request.NameLength = VirtGpuContextNameLength(DebugName);
    VideoPortMoveMemory(Request.DebugName, (PVOID)DebugName, sizeof(Request.DebugName));

    if (DeviceExtension->ContextInitSupported)
    {
        Request.ContextInit = ContextInit;
        if ((Request.ContextInit == 0) && (CapsetId != 0))
            Request.ContextInit = CapsetId & VIRTGPU_CONTEXT_INIT_CAPSET_ID_MASK;
    }

    if (!VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request)))
        return FALSE;

    Slot->InUse = TRUE;
    Slot->ContextId = Id;
    *ContextId = Id;
    return TRUE;
}

static BOOLEAN
VirtGpuDestroyContext(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ContextId)
{
    PVIRTGPU_3D_CONTEXT_STATE Slot;
    VIRTGPU_CTX_DESTROY Request;

    Slot = VirtGpuFindContext(DeviceExtension, ContextId);
    if (Slot == NULL)
        return FALSE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_CTX_DESTROY;
    Request.Header.ContextId = ContextId;

    if (!VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request)))
        return FALSE;

    VideoPortZeroMemory(Slot, sizeof(*Slot));
    return TRUE;
}

static PVIRTGPU_3D_RESOURCE_STATE
VirtGpuFindResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_MAX_3D_RESOURCES; ++Index)
    {
        if (DeviceExtension->Resources[Index].InUse &&
            (DeviceExtension->Resources[Index].ResourceId == ResourceId))
        {
            return &DeviceExtension->Resources[Index];
        }
    }

    return NULL;
}

static PVIRTGPU_3D_RESOURCE_STATE
VirtGpuFindFreeResource(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;

    for (Index = 0; Index < VIRTGPU_MAX_3D_RESOURCES; ++Index)
    {
        if (!DeviceExtension->Resources[Index].InUse)
            return &DeviceExtension->Resources[Index];
    }

    return NULL;
}

static ULONG
VirtGpuAllocateResourceId(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Attempt;

    for (Attempt = 0; Attempt < VIRTGPU_MAX_3D_RESOURCES * 4; ++Attempt)
    {
        ULONG ResourceId = DeviceExtension->NextResourceId++;

        if (DeviceExtension->NextResourceId < VIRTGPU_FIRST_3D_RESOURCE_ID)
            DeviceExtension->NextResourceId = VIRTGPU_FIRST_3D_RESOURCE_ID;

        if ((ResourceId >= VIRTGPU_FIRST_3D_RESOURCE_ID) &&
            (VirtGpuFindResource(DeviceExtension, ResourceId) == NULL))
        {
            return ResourceId;
        }
    }

    return 0;
}

static BOOLEAN
VirtGpuRangeValid(
    _In_ ULONGLONG Offset,
    _In_ ULONG Size,
    _In_ ULONG Limit)
{
    if (Offset > Limit)
        return FALSE;

    if (Size > Limit - (ULONG)Offset)
        return FALSE;

    return TRUE;
}

static BOOLEAN
VirtGpuTransferPackedRows(
    _In_ PVIRTGPU_3D_TRANSFER Transfer,
    _Out_ PULONG RowBytes)
{
    if ((Transfer == NULL) ||
        (RowBytes == NULL) ||
        (Transfer->Depth != 1) ||
        (Transfer->Height == 0) ||
        (Transfer->Size == 0) ||
        (Transfer->Stride == 0) ||
        ((Transfer->Size % Transfer->Height) != 0))
    {
        return FALSE;
    }

    *RowBytes = Transfer->Size / Transfer->Height;
    return (*RowBytes != 0) && (*RowBytes <= Transfer->Stride);
}

static BOOLEAN
VirtGpuTransferBackingRangeValid(
    _In_ PVIRTGPU_3D_TRANSFER Transfer,
    _In_ ULONG BackingSize)
{
    ULONG RowBytes;
    ULONGLONG LastRowOffset;
    ULONGLONG EndOffset;

    if (Transfer == NULL)
        return FALSE;

    if (!VirtGpuTransferPackedRows(Transfer, &RowBytes))
        return VirtGpuRangeValid(Transfer->Offset, Transfer->Size, BackingSize);

    LastRowOffset = Transfer->Offset +
                    ((ULONGLONG)(Transfer->Height - 1) * Transfer->Stride);
    EndOffset = LastRowOffset + RowBytes;
    return (LastRowOffset >= Transfer->Offset) &&
           (EndOffset >= LastRowOffset) &&
           (EndOffset <= BackingSize);
}

static VOID
VirtGpuCopyTransferDataToBacking(
    _In_ PVIRTGPU_3D_RESOURCE_STATE Resource,
    _In_ PVIRTGPU_3D_TRANSFER Transfer,
    _In_reads_bytes_(Transfer->Size) const UCHAR* Data)
{
    ULONG RowBytes;
    ULONG Row;

    if ((Resource == NULL) ||
        (Transfer == NULL) ||
        (Data == NULL) ||
        (Transfer->Size == 0))
    {
        return;
    }

    if (VirtGpuTransferPackedRows(Transfer, &RowBytes))
    {
        for (Row = 0; Row < Transfer->Height; ++Row)
        {
            VideoPortMoveMemory((PUCHAR)Resource->BackingVirtual +
                                (ULONG)Transfer->Offset +
                                (Row * Transfer->Stride),
                                (PVOID)(Data + (Row * RowBytes)),
                                RowBytes);
        }
        return;
    }

    VideoPortMoveMemory((PUCHAR)Resource->BackingVirtual +
                        (ULONG)Transfer->Offset,
                        (PVOID)Data,
                        Transfer->Size);
}

static VOID
VirtGpuCopyTransferToBacking(
    _In_ PVIRTGPU_3D_RESOURCE_STATE Resource,
    _In_ PVIRTGPU_3D_TRANSFER Transfer)
{
    VirtGpuCopyTransferDataToBacking(Resource, Transfer, Transfer->Data);
}

static VOID
VirtGpuCopyBackingToTransferData(
    _In_ PVIRTGPU_3D_RESOURCE_STATE Resource,
    _In_ PVIRTGPU_3D_TRANSFER Transfer,
    _Out_writes_bytes_(Transfer->Size) UCHAR* Data)
{
    ULONG RowBytes;
    ULONG Row;

    if ((Resource == NULL) ||
        (Transfer == NULL) ||
        (Data == NULL) ||
        (Transfer->Size == 0))
    {
        return;
    }

    if (VirtGpuTransferPackedRows(Transfer, &RowBytes))
    {
        for (Row = 0; Row < Transfer->Height; ++Row)
        {
            VideoPortMoveMemory(Data + (Row * RowBytes),
                                (PUCHAR)Resource->BackingVirtual +
                                (ULONG)Transfer->Offset +
                                (Row * Transfer->Stride),
                                RowBytes);
        }
        return;
    }

    VideoPortMoveMemory(Data,
                        (PUCHAR)Resource->BackingVirtual +
                        (ULONG)Transfer->Offset,
                        Transfer->Size);
}

static VOID
VirtGpuCopyBackingToTransfer(
    _In_ PVIRTGPU_3D_RESOURCE_STATE Resource,
    _In_ PVIRTGPU_3D_TRANSFER Transfer)
{
    VirtGpuCopyBackingToTransferData(Resource, Transfer, Transfer->Data);
}

static BOOLEAN
VirtGpuAttachResourceBacking(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTGPU_3D_RESOURCE_STATE Resource)
{
    struct
    {
        VIRTGPU_RESOURCE_ATTACH_BACKING Attach;
        VIRTGPU_MEM_ENTRY Entry;
    } Request;

    if ((Resource->BackingVirtual == NULL) || (Resource->BackingSize == 0))
        return FALSE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Attach.Header.Type = VIRTGPU_CMD_RESOURCE_ATTACH_BACKING;
    Request.Attach.ResourceId = Resource->ResourceId;
    Request.Attach.EntryCount = 1;
    Request.Entry.Address = Resource->BackingPhysical.QuadPart;
    Request.Entry.Length = Resource->BackingSize;

    if (!VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request)))
        return FALSE;

    Resource->BackingAttached = TRUE;
    return TRUE;
}

static BOOLEAN
VirtGpuDetachResourceBacking(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTGPU_3D_RESOURCE_STATE Resource)
{
    VIRTGPU_RESOURCE_DETACH_BACKING Request;

    if (!Resource->BackingAttached)
        return TRUE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_RESOURCE_DETACH_BACKING;
    Request.ResourceId = Resource->ResourceId;

    if (!VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request)))
        return FALSE;

    Resource->BackingAttached = FALSE;
    return TRUE;
}

static BOOLEAN
VirtGpuUnrefResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId)
{
    VIRTGPU_RESOURCE_UNREF Request;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_RESOURCE_UNREF;
    Request.ResourceId = ResourceId;
    return VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request));
}

static BOOLEAN
VirtGpuCreate3DResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTGPU_3D_CREATE_RESOURCE Create)
{
    PVIRTGPU_3D_RESOURCE_STATE Slot;
    VIRTGPU_RESOURCE_CREATE_3D Request;
    ULONG ResourceId;

    if (!DeviceExtension->VirglSupported ||
        (Create == NULL) ||
        (Create->Width == 0) ||
        (Create->Height == 0) ||
        (Create->Depth == 0) ||
        (Create->BackingSize > VIRTGPU_MAX_3D_BACKING_SIZE))
    {
        return FALSE;
    }

    Slot = VirtGpuFindFreeResource(DeviceExtension);
    if (Slot == NULL)
        return FALSE;

    ResourceId = Create->ResourceId;
    if (ResourceId == 0)
    {
        ResourceId = VirtGpuAllocateResourceId(DeviceExtension);
        if (ResourceId == 0)
            return FALSE;
    }
    else if ((ResourceId < VIRTGPU_FIRST_3D_RESOURCE_ID) ||
             (VirtGpuFindResource(DeviceExtension, ResourceId) != NULL))
    {
        return FALSE;
    }

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_RESOURCE_CREATE_3D;
    Request.ResourceId = ResourceId;
    Request.Target = Create->Target;
    Request.Format = Create->Format;
    Request.Bind = Create->Bind;
    Request.Width = Create->Width;
    Request.Height = Create->Height;
    Request.Depth = Create->Depth;
    Request.ArraySize = Create->ArraySize;
    Request.LastLevel = Create->LastLevel;
    Request.NrSamples = Create->NrSamples;
    Request.Flags = Create->Flags;

    if (!VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request)))
        return FALSE;

    VideoPortZeroMemory(Slot, sizeof(*Slot));
    Slot->InUse = TRUE;
    Slot->ResourceId = ResourceId;

    if (Create->BackingSize != 0)
    {
        Slot->BackingVirtual =
            VirtGpuAllocateContiguous(DeviceExtension,
                                      Create->BackingSize,
                                      &Slot->BackingPhysical);
        if (Slot->BackingVirtual == NULL)
        {
            VirtGpuUnrefResource(DeviceExtension, ResourceId);
            VideoPortZeroMemory(Slot, sizeof(*Slot));
            return FALSE;
        }

        Slot->BackingSize = ROUND_TO_PAGES(Create->BackingSize);
        if (!VirtGpuAttachResourceBacking(DeviceExtension, Slot))
        {
            VirtGpuUnrefResource(DeviceExtension, ResourceId);
            MmFreeContiguousMemory(Slot->BackingVirtual);
            VideoPortZeroMemory(Slot, sizeof(*Slot));
            return FALSE;
        }
    }

    Create->ResourceId = ResourceId;
    return TRUE;
}

static BOOLEAN
VirtGpuBlobMemNeedsBacking(_In_ ULONG BlobMem)
{
    return (BlobMem == VIRTGPU_BLOB_MEM_GUEST) ||
           (BlobMem == VIRTGPU_BLOB_MEM_HOST3D_GUEST);
}

static BOOLEAN
VirtGpuSubmit3D(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ContextId,
    _In_reads_bytes_(Size) PVOID Commands,
    _In_ ULONG Size,
    _In_ ULONG RingIndex,
    _Out_opt_ PULONGLONG FenceId);

static BOOLEAN
VirtGpuCreateBlobResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTGPU_3D_CREATE_BLOB Create,
    _In_reads_bytes_opt_(CommandSize) PVOID Commands,
    _In_ ULONG CommandSize)
{
    PVIRTGPU_3D_RESOURCE_STATE Slot;
    VIRTGPU_RESOURCE_CREATE_BLOB Request;
    VIRTGPU_MEM_ENTRY Entry;
    ULONG ResourceId;
    ULONG BackingSize;
    ULONG EntryCount = 0;
    BOOLEAN Host3DBlob;

    if (!DeviceExtension->ResourceBlobSupported ||
        (Create == NULL) ||
        (Create->Size == 0) ||
        (Create->BlobFlags & ~VIRTGPU_BLOB_FLAG_USE_MASK) ||
        ((CommandSize != 0) && (Commands == NULL)) ||
        ((CommandSize != 0) && ((CommandSize & 3) != 0)) ||
        (CommandSize > VIRTGPU_RESPONSE_OFFSET - sizeof(VIRTGPU_SUBMIT_3D)))
    {
        return FALSE;
    }

    if ((Create->BlobFlags & VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE) &&
        !DeviceExtension->ResourceUuidSupported)
    {
        return FALSE;
    }

    switch (Create->BlobMem)
    {
        case VIRTGPU_BLOB_MEM_GUEST:
        case VIRTGPU_BLOB_MEM_HOST3D:
        case VIRTGPU_BLOB_MEM_HOST3D_GUEST:
            break;
        default:
            return FALSE;
    }

    Host3DBlob = (Create->BlobMem == VIRTGPU_BLOB_MEM_HOST3D) ||
                 (Create->BlobMem == VIRTGPU_BLOB_MEM_HOST3D_GUEST);
    if (Host3DBlob)
    {
        if (!DeviceExtension->VirglSupported ||
            (Create->ContextId == 0) ||
            (VirtGpuFindContext(DeviceExtension, Create->ContextId) == NULL))
        {
            return FALSE;
        }
    }
    else
    {
        if ((Create->ContextId != 0) ||
            (Create->BlobId != 0) ||
            (CommandSize != 0))
        {
            return FALSE;
        }
    }

    BackingSize = Create->BackingSize;
    if (VirtGpuBlobMemNeedsBacking(Create->BlobMem))
    {
        if (BackingSize == 0)
        {
            if (Create->Size > VIRTGPU_MAX_3D_BACKING_SIZE)
                return FALSE;
            BackingSize = (ULONG)Create->Size;
        }
        if ((BackingSize == 0) || (BackingSize > VIRTGPU_MAX_3D_BACKING_SIZE))
            return FALSE;
    }
    else if (BackingSize != 0)
    {
        return FALSE;
    }

    Slot = VirtGpuFindFreeResource(DeviceExtension);
    if (Slot == NULL)
        return FALSE;

    ResourceId = Create->ResourceId;
    if (ResourceId == 0)
    {
        ResourceId = VirtGpuAllocateResourceId(DeviceExtension);
        if (ResourceId == 0)
            return FALSE;
    }
    else if ((ResourceId < VIRTGPU_FIRST_3D_RESOURCE_ID) ||
             (VirtGpuFindResource(DeviceExtension, ResourceId) != NULL))
    {
            return FALSE;
    }

    if (CommandSize != 0)
    {
        ULONGLONG FenceId = 0;

        if (!VirtGpuSubmit3D(DeviceExtension,
                             Create->ContextId,
                             Commands,
                             CommandSize,
                             0,
                             &FenceId))
        {
            return FALSE;
        }
    }

    VideoPortZeroMemory(Slot, sizeof(*Slot));
    Slot->InUse = TRUE;
    Slot->Blob = TRUE;
    Slot->ResourceId = ResourceId;
    Slot->BlobMem = Create->BlobMem;
    Slot->BlobFlags = Create->BlobFlags;
    Slot->BlobSize = Create->Size;
    Slot->BlobId = Create->BlobId;

    if (BackingSize != 0)
    {
        Slot->BackingVirtual =
            VirtGpuAllocateContiguous(DeviceExtension,
                                      BackingSize,
                                      &Slot->BackingPhysical);
        if (Slot->BackingVirtual == NULL)
        {
            VideoPortZeroMemory(Slot, sizeof(*Slot));
            return FALSE;
        }

        Slot->BackingSize = ROUND_TO_PAGES(BackingSize);
        EntryCount = 1;
    }

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_RESOURCE_CREATE_BLOB;
    Request.Header.ContextId = Create->ContextId;
    Request.ResourceId = ResourceId;
    Request.BlobMem = Create->BlobMem;
    Request.BlobFlags = Create->BlobFlags;
    Request.EntryCount = EntryCount;
    Request.BlobId = Create->BlobId;
    Request.Size = Create->Size;

    if (EntryCount != 0)
    {
        VideoPortZeroMemory(&Entry, sizeof(Entry));
        Entry.Address = Slot->BackingPhysical.QuadPart;
        Entry.Length = Slot->BackingSize;

        if (!VirtGpuCommandOkWithPayload(DeviceExtension,
                                         &Request,
                                         sizeof(Request),
                                         &Entry,
                                         sizeof(Entry)))
        {
            MmFreeContiguousMemory(Slot->BackingVirtual);
            VideoPortZeroMemory(Slot, sizeof(*Slot));
            return FALSE;
        }

        Slot->BackingAttached = TRUE;
    }
    else if (!VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request)))
    {
        VideoPortZeroMemory(Slot, sizeof(*Slot));
        return FALSE;
    }

    Create->ResourceId = ResourceId;
    return TRUE;
}

static BOOLEAN
VirtGpuAssignResourceUuid(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTGPU_3D_RESOURCE_UUID ResourceUuid)
{
    PVIRTGPU_3D_RESOURCE_STATE Slot;
    VIRTGPU_RESOURCE_ASSIGN_UUID Request;
    PVIRTGPU_RESP_RESOURCE_UUID Response;

    if (!DeviceExtension->ResourceUuidSupported || (ResourceUuid == NULL))
        return FALSE;

    Slot = VirtGpuFindResource(DeviceExtension, ResourceUuid->ResourceId);
    if (Slot == NULL)
        return FALSE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_RESOURCE_ASSIGN_UUID;
    Request.ResourceId = ResourceUuid->ResourceId;

    if (!VirtGpuSendCommand(DeviceExtension,
                            &Request,
                            sizeof(Request),
                            sizeof(*Response),
                            (PVOID*)&Response))
    {
        return FALSE;
    }

    if (Response->Header.Type != VIRTGPU_RESP_OK_RESOURCE_UUID)
        return FALSE;

    VideoPortMoveMemory(ResourceUuid->Uuid, Response->Uuid, sizeof(ResourceUuid->Uuid));
    VideoPortMoveMemory(Slot->Uuid, Response->Uuid, sizeof(Slot->Uuid));
    Slot->UuidValid = TRUE;
    return TRUE;
}

static BOOLEAN
VirtGpuMapBlobResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PVIRTGPU_3D_MAP_BLOB Map)
{
    PVIRTGPU_3D_RESOURCE_STATE Slot;
    VIRTGPU_RESOURCE_MAP_BLOB Request;
    PVIRTGPU_RESP_MAP_INFO Response;

    if (!DeviceExtension->ResourceBlobSupported || (Map == NULL))
        return FALSE;

    Slot = VirtGpuFindResource(DeviceExtension, Map->ResourceId);
    if ((Slot == NULL) || !Slot->Blob)
        return FALSE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_RESOURCE_MAP_BLOB;
    Request.ResourceId = Map->ResourceId;
    Request.Offset = Map->Offset;

    if (!VirtGpuSendCommand(DeviceExtension,
                            &Request,
                            sizeof(Request),
                            sizeof(*Response),
                            (PVOID*)&Response))
    {
        return FALSE;
    }

    if (Response->Header.Type != VIRTGPU_RESP_OK_MAP_INFO)
        return FALSE;

    Map->MapInfo = Response->MapInfo;
    Slot->BlobMapInfo = Response->MapInfo;
    Slot->BlobMapped = TRUE;
    return TRUE;
}

static BOOLEAN
VirtGpuUnmapBlobResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId)
{
    PVIRTGPU_3D_RESOURCE_STATE Slot;
    VIRTGPU_RESOURCE_UNMAP_BLOB Request;

    if (!DeviceExtension->ResourceBlobSupported)
        return FALSE;

    Slot = VirtGpuFindResource(DeviceExtension, ResourceId);
    if ((Slot == NULL) || !Slot->Blob)
        return FALSE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_RESOURCE_UNMAP_BLOB;
    Request.ResourceId = ResourceId;

    if (!VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request)))
        return FALSE;

    Slot->BlobMapped = FALSE;
    Slot->BlobMapInfo = 0;
    return TRUE;
}

static BOOLEAN
VirtGpuDestroy3DResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId)
{
    PVIRTGPU_3D_RESOURCE_STATE Slot;
    BOOLEAN Success = TRUE;

    Slot = VirtGpuFindResource(DeviceExtension, ResourceId);
    if (Slot == NULL)
        return FALSE;

    if (Slot->BlobMapped && !VirtGpuUnmapBlobResource(DeviceExtension, ResourceId))
        Success = FALSE;

    if (Slot->BackingAttached)
        Success = VirtGpuDetachResourceBacking(DeviceExtension, Slot);

    if (!VirtGpuUnrefResource(DeviceExtension, ResourceId))
        Success = FALSE;

    if (Slot->BackingVirtual != NULL)
        MmFreeContiguousMemory(Slot->BackingVirtual);

    VideoPortZeroMemory(Slot, sizeof(*Slot));
    return Success;
}

static BOOLEAN
VirtGpuContextResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Attach,
    _In_ ULONG ContextId,
    _In_ ULONG ResourceId)
{
    VIRTGPU_CTX_RESOURCE Request;

    if (!DeviceExtension->VirglSupported ||
        (VirtGpuFindContext(DeviceExtension, ContextId) == NULL) ||
        (VirtGpuFindResource(DeviceExtension, ResourceId) == NULL))
    {
        return FALSE;
    }

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = Attach ?
        VIRTGPU_CMD_CTX_ATTACH_RESOURCE :
        VIRTGPU_CMD_CTX_DETACH_RESOURCE;
    Request.Header.ContextId = ContextId;
    Request.ResourceId = ResourceId;

    return VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request));
}

static BOOLEAN
VirtGpuBuildTransferHost3D(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN ToHost,
    _In_ PVIRTGPU_3D_TRANSFER Transfer,
    _Out_ PVIRTGPU_TRANSFER_HOST_3D Request,
    _Out_opt_ PULONGLONG FenceId)
{
    if ((Transfer->Width == 0) ||
        (Transfer->Height == 0) ||
        (Transfer->Depth == 0) ||
        (Transfer->RingIndex > 0xFF) ||
        ((Transfer->RingIndex != 0) && !DeviceExtension->ContextInitSupported) ||
        ((Transfer->ContextId != 0) &&
         (VirtGpuFindContext(DeviceExtension, Transfer->ContextId) == NULL)))
    {
        return FALSE;
    }

    VideoPortZeroMemory(Request, sizeof(*Request));
    Request->Header.Type = ToHost ?
        VIRTGPU_CMD_TRANSFER_TO_HOST_3D :
        VIRTGPU_CMD_TRANSFER_FROM_HOST_3D;
    Request->Header.ContextId = Transfer->ContextId;
    VirtGpuPrepareFence(DeviceExtension,
                        &Request->Header,
                        Transfer->RingIndex,
                        FenceId);
    Request->Box.X = Transfer->X;
    Request->Box.Y = Transfer->Y;
    Request->Box.Z = Transfer->Z;
    Request->Box.Width = Transfer->Width;
    Request->Box.Height = Transfer->Height;
    Request->Box.Depth = Transfer->Depth;
    Request->Offset = Transfer->Offset;
    Request->ResourceId = Transfer->ResourceId;
    Request->Level = Transfer->Level;
    Request->Stride = Transfer->Stride;
    Request->LayerStride = Transfer->LayerStride;

    return TRUE;
}

static BOOLEAN
VirtGpuTransferHost3D(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN ToHost,
    _In_ PVIRTGPU_3D_TRANSFER Transfer,
    _Out_opt_ PULONGLONG FenceId)
{
    VIRTGPU_TRANSFER_HOST_3D Request;

    if (!VirtGpuBuildTransferHost3D(DeviceExtension,
                                    ToHost,
                                    Transfer,
                                    &Request,
                                    FenceId))
    {
        return FALSE;
    }

    return VirtGpuCommandOk(DeviceExtension, &Request, sizeof(Request));
}

static BOOLEAN
VirtGpuTransferHost3DAsync(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PVIRTGPU_3D_TRANSFER Transfer,
    _Out_opt_ PULONGLONG FenceId)
{
    VIRTGPU_TRANSFER_HOST_3D Request;

    if (!VirtGpuBuildTransferHost3D(DeviceExtension,
                                    TRUE,
                                    Transfer,
                                    &Request,
                                    FenceId))
    {
        return FALSE;
    }

    return VirtGpuSendCommandAsync(DeviceExtension, &Request, sizeof(Request));
}

static BOOLEAN
VirtGpuSubmit3D(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ContextId,
    _In_reads_bytes_(Size) PVOID Commands,
    _In_ ULONG Size,
    _In_ ULONG RingIndex,
    _Out_opt_ PULONGLONG FenceId)
{
    VIRTGPU_SUBMIT_3D Request;

    if (!DeviceExtension->VirglSupported ||
        (VirtGpuFindContext(DeviceExtension, ContextId) == NULL) ||
        (Commands == NULL) ||
        (Size == 0) ||
        (RingIndex > 0xFF) ||
        ((RingIndex != 0) && !DeviceExtension->ContextInitSupported) ||
        (Size > VIRTGPU_RESPONSE_OFFSET - sizeof(Request)))
    {
        return FALSE;
    }

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_SUBMIT_3D;
    Request.Header.ContextId = ContextId;
    VirtGpuPrepareFence(DeviceExtension, &Request.Header, RingIndex, FenceId);
    Request.Size = Size;

    return VirtGpuCommandOkWithPayload(DeviceExtension,
                                       &Request,
                                       sizeof(Request),
                                       Commands,
                                       Size);
}

static BOOLEAN
VirtGpuSubmit3DAsync(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ContextId,
    _In_reads_bytes_(Size) PVOID Commands,
    _In_ ULONG Size,
    _In_ ULONG RingIndex,
    _Out_opt_ PULONGLONG FenceId)
{
    union
    {
        VIRTGPU_SUBMIT_3D Request;
        UCHAR Buffer[VIRTGPU_ASYNC_RESPONSE_OFFSET];
    } Submit;
    ULONG RequestSize;

    if (!DeviceExtension->VirglSupported ||
        (VirtGpuFindContext(DeviceExtension, ContextId) == NULL) ||
        (Commands == NULL) ||
        (Size == 0) ||
        (RingIndex > 0xFF) ||
        ((RingIndex != 0) && !DeviceExtension->ContextInitSupported) ||
        (Size > VIRTGPU_ASYNC_RESPONSE_OFFSET - sizeof(VIRTGPU_SUBMIT_3D)))
    {
        return FALSE;
    }

    RequestSize = sizeof(VIRTGPU_SUBMIT_3D) + Size;
    VideoPortZeroMemory(&Submit, sizeof(Submit));
    Submit.Request.Header.Type = VIRTGPU_CMD_SUBMIT_3D;
    Submit.Request.Header.ContextId = ContextId;
    VirtGpuPrepareFence(DeviceExtension, &Submit.Request.Header, RingIndex, FenceId);
    Submit.Request.Size = Size;
    VideoPortMoveMemory(Submit.Buffer + sizeof(VIRTGPU_SUBMIT_3D),
                        Commands,
                        Size);

    return VirtGpuSendCommandAsync(DeviceExtension,
                                   Submit.Buffer,
                                   RequestSize);
}

static BOOLEAN
VirtGpuExecute3DBatchSubmit(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ContextId,
    _In_reads_bytes_(CommandSize) PUCHAR Commands,
    _In_ ULONG CommandSize,
    _In_ ULONG RingIndex,
    _Out_ PULONGLONG LastFenceId)
{
    ULONGLONG FenceId = 0;

    if ((Commands == NULL) || (CommandSize == 0))
        return FALSE;

    if (!(VirtGpuSubmit3DAsync(DeviceExtension,
                               ContextId,
                               Commands,
                               CommandSize,
                               RingIndex,
                               &FenceId) ||
          VirtGpuSubmit3D(DeviceExtension,
                          ContextId,
                          Commands,
                          CommandSize,
                          RingIndex,
                          &FenceId)))
    {
        return FALSE;
    }

    if (FenceId != 0)
        *LastFenceId = FenceId;
    return TRUE;
}

static BOOLEAN
VirtGpuExecute3DBatchTransferToHost(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ContextId,
    _In_reads_bytes_(CommandSize) PUCHAR Command,
    _In_ ULONG CommandSize,
    _In_ ULONG RingIndex,
    _Out_ PULONGLONG LastFenceId)
{
    VIRTGPU_3D_BATCH_TRANSFER BatchTransfer;
    VIRTGPU_3D_TRANSFER Transfer;
    PVIRTGPU_3D_RESOURCE_STATE Resource;
    ULONG HeaderSize = offsetof(VIRTGPU_3D_BATCH_TRANSFER, Data);
    ULONGLONG FenceId = 0;

    if ((Command == NULL) || (CommandSize < HeaderSize))
        return FALSE;

    VideoPortMoveMemory(&BatchTransfer, Command, HeaderSize);
    if (BatchTransfer.Size > CommandSize - HeaderSize)
        return FALSE;

    VideoPortZeroMemory(&Transfer, sizeof(Transfer));
    Transfer.ContextId = ContextId;
    Transfer.ResourceId = BatchTransfer.ResourceId;
    Transfer.X = BatchTransfer.X;
    Transfer.Y = BatchTransfer.Y;
    Transfer.Z = BatchTransfer.Z;
    Transfer.Width = BatchTransfer.Width;
    Transfer.Height = BatchTransfer.Height;
    Transfer.Depth = BatchTransfer.Depth;
    Transfer.Level = BatchTransfer.Level;
    Transfer.Stride = BatchTransfer.Stride;
    Transfer.LayerStride = BatchTransfer.LayerStride;
    Transfer.Offset = BatchTransfer.Offset;
    Transfer.RingIndex = RingIndex;
    Transfer.Size = BatchTransfer.Size;

    Resource = VirtGpuFindResource(DeviceExtension, Transfer.ResourceId);
    if ((Resource == NULL) ||
        !Resource->BackingAttached ||
        !VirtGpuTransferBackingRangeValid(&Transfer, Resource->BackingSize))
    {
        return FALSE;
    }

    VirtGpuCopyTransferDataToBacking(Resource, &Transfer, Command + HeaderSize);

    if (!(VirtGpuTransferHost3DAsync(DeviceExtension, &Transfer, &FenceId) ||
          VirtGpuTransferHost3D(DeviceExtension, TRUE, &Transfer, &FenceId)))
    {
        return FALSE;
    }

    if (FenceId != 0)
        *LastFenceId = FenceId;
    return TRUE;
}

static BOOLEAN
VirtGpuExecute3DBatchTransferFromHost(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ContextId,
    _Inout_updates_bytes_(CommandSize) PUCHAR Command,
    _In_ ULONG CommandSize,
    _In_ ULONG RingIndex,
    _Out_ PULONGLONG LastFenceId)
{
    VIRTGPU_3D_BATCH_TRANSFER BatchTransfer;
    VIRTGPU_3D_TRANSFER Transfer;
    PVIRTGPU_3D_RESOURCE_STATE Resource;
    ULONG HeaderSize = offsetof(VIRTGPU_3D_BATCH_TRANSFER, Data);
    ULONGLONG FenceId = 0;

    if ((Command == NULL) || (CommandSize < HeaderSize))
        return FALSE;

    VideoPortMoveMemory(&BatchTransfer, Command, HeaderSize);
    if (BatchTransfer.Size > CommandSize - HeaderSize)
        return FALSE;

    VideoPortZeroMemory(&Transfer, sizeof(Transfer));
    Transfer.ContextId = ContextId;
    Transfer.ResourceId = BatchTransfer.ResourceId;
    Transfer.X = BatchTransfer.X;
    Transfer.Y = BatchTransfer.Y;
    Transfer.Z = BatchTransfer.Z;
    Transfer.Width = BatchTransfer.Width;
    Transfer.Height = BatchTransfer.Height;
    Transfer.Depth = BatchTransfer.Depth;
    Transfer.Level = BatchTransfer.Level;
    Transfer.Stride = BatchTransfer.Stride;
    Transfer.LayerStride = BatchTransfer.LayerStride;
    Transfer.Offset = BatchTransfer.Offset;
    Transfer.RingIndex = RingIndex;
    Transfer.Size = BatchTransfer.Size;

    Resource = VirtGpuFindResource(DeviceExtension, Transfer.ResourceId);
    if ((Resource == NULL) ||
        !Resource->BackingAttached ||
        !VirtGpuTransferBackingRangeValid(&Transfer, Resource->BackingSize))
    {
        return FALSE;
    }

    if (!VirtGpuTransferHost3D(DeviceExtension, FALSE, &Transfer, &FenceId))
        return FALSE;

    VirtGpuCopyBackingToTransferData(Resource,
                                     &Transfer,
                                     Command + HeaderSize);

    if (FenceId != 0)
        *LastFenceId = FenceId;
    return TRUE;
}

static BOOLEAN
VirtGpuExecute3DBatch(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_updates_bytes_(InputSize) PVIRTGPU_3D_BATCH Batch,
    _In_ ULONG InputSize)
{
    ULONG HeaderSize = offsetof(VIRTGPU_3D_BATCH, Commands);
    PUCHAR Cursor;
    PUCHAR End;
    ULONG Index;
    ULONGLONG LastFenceId = 0;

    if (!DeviceExtension->VirglSupported ||
        (Batch == NULL) ||
        (InputSize < HeaderSize) ||
        (Batch->Version != VIRTGPU_3D_BATCH_VERSION) ||
        (Batch->ContextId == 0) ||
        (VirtGpuFindContext(DeviceExtension, Batch->ContextId) == NULL) ||
        (Batch->RingIndex > 0xFF) ||
        ((Batch->RingIndex != 0) && !DeviceExtension->ContextInitSupported) ||
        (Batch->CommandCount > VIRTGPU_3D_MAX_BATCH_COMMANDS) ||
        (Batch->Size > VIRTGPU_3D_MAX_BATCH_BYTES) ||
        (Batch->Size > InputSize - HeaderSize))
    {
        return FALSE;
    }

    Cursor = Batch->Commands;
    End = Cursor + Batch->Size;

    for (Index = 0; Index < Batch->CommandCount; ++Index)
    {
        VIRTGPU_3D_BATCH_COMMAND Command;
        PUCHAR Payload;

        if ((ULONG_PTR)(End - Cursor) < sizeof(Command))
            return FALSE;

        VideoPortMoveMemory(&Command, Cursor, sizeof(Command));
        Cursor += sizeof(Command);

        if ((Command.Size & 3) != 0)
            return FALSE;
        if ((ULONG_PTR)(End - Cursor) < Command.Size)
            return FALSE;

        Payload = Cursor;
        switch (Command.OpCode)
        {
            case VIRTGPU_3D_BATCH_OP_NOP:
                if (Command.Size != 0)
                    return FALSE;
                break;

            case VIRTGPU_3D_BATCH_OP_SUBMIT:
                if (!VirtGpuExecute3DBatchSubmit(DeviceExtension,
                                                 Batch->ContextId,
                                                 Payload,
                                                 Command.Size,
                                                 Batch->RingIndex,
                                                 &LastFenceId))
                {
                    return FALSE;
                }
                break;

            case VIRTGPU_3D_BATCH_OP_TRANSFER_TO_HOST:
                if (!VirtGpuExecute3DBatchTransferToHost(DeviceExtension,
                                                         Batch->ContextId,
                                                         Payload,
                                                         Command.Size,
                                                         Batch->RingIndex,
                                                         &LastFenceId))
                {
                    return FALSE;
                }
                break;

            case VIRTGPU_3D_BATCH_OP_TRANSFER_FROM_HOST:
                if (!VirtGpuExecute3DBatchTransferFromHost(DeviceExtension,
                                                           Batch->ContextId,
                                                           Payload,
                                                           Command.Size,
                                                           Batch->RingIndex,
                                                           &LastFenceId))
                {
                    return FALSE;
                }
                break;

            default:
                return FALSE;
        }

        Cursor += Command.Size;
    }

    if (Cursor != End)
        return FALSE;

    Batch->FenceId = LastFenceId;
    return TRUE;
}

static VOID
VirtGpuSetRectAt(
    _Out_ PVIRTGPU_RECT Rect,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height);

static BOOLEAN
VirtGpuSetScanoutResource(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    VIRTGPU_SET_SCANOUT ScanoutRequest;

    if ((ResourceId != VIRTGPU_RESOURCE_ID) &&
        (VirtGpuFindResource(DeviceExtension, ResourceId) == NULL))
    {
        return FALSE;
    }

    if ((Width == 0) || (Height == 0))
        return FALSE;

    VideoPortZeroMemory(&ScanoutRequest, sizeof(ScanoutRequest));
    ScanoutRequest.Header.Type = VIRTGPU_CMD_SET_SCANOUT;
    VirtGpuSetRectAt(&ScanoutRequest.Rect, X, Y, Width, Height);
    ScanoutRequest.ScanoutId = VIRTGPU_SCANOUT_ID;
    ScanoutRequest.ResourceId = ResourceId;
    return VirtGpuCommandOk(DeviceExtension, &ScanoutRequest, sizeof(ScanoutRequest));
}

static BOOLEAN
VirtGpuSetScanout(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    return VirtGpuSetScanoutResource(DeviceExtension,
                                     VIRTGPU_RESOURCE_ID,
                                     0,
                                     0,
                                     DeviceExtension->ScreenWidth,
                                     DeviceExtension->ScreenHeight);
}

static VOID
VirtGpuSetRectAt(
    _Out_ PVIRTGPU_RECT Rect,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    Rect->X = X;
    Rect->Y = Y;
    Rect->Width = Width;
    Rect->Height = Height;
}

static BOOLEAN
VirtGpuCreate2DResource(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    struct
    {
        VIRTGPU_RESOURCE_ATTACH_BACKING Attach;
        VIRTGPU_MEM_ENTRY Entry;
    } AttachRequest;
    VIRTGPU_RESOURCE_CREATE_2D CreateRequest;

    VideoPortZeroMemory(&CreateRequest, sizeof(CreateRequest));
    CreateRequest.Header.Type = VIRTGPU_CMD_RESOURCE_CREATE_2D;
    CreateRequest.ResourceId = VIRTGPU_RESOURCE_ID;
    CreateRequest.Format = VIRTGPU_FORMAT_B8G8R8X8_UNORM;
    CreateRequest.Width = DeviceExtension->MaxScreenWidth;
    CreateRequest.Height = DeviceExtension->MaxScreenHeight;
    if (!VirtGpuCommandOk(DeviceExtension, &CreateRequest, sizeof(CreateRequest)))
        return FALSE;

    VideoPortZeroMemory(&AttachRequest, sizeof(AttachRequest));
    AttachRequest.Attach.Header.Type = VIRTGPU_CMD_RESOURCE_ATTACH_BACKING;
    AttachRequest.Attach.ResourceId = VIRTGPU_RESOURCE_ID;
    AttachRequest.Attach.EntryCount = 1;
    AttachRequest.Entry.Address = DeviceExtension->FrameBufferPhysical.QuadPart;
    AttachRequest.Entry.Length = DeviceExtension->FrameBufferSize;
    if (!VirtGpuCommandOk(DeviceExtension, &AttachRequest, sizeof(AttachRequest)))
        return FALSE;

    return VirtGpuSetScanout(DeviceExtension);
}

static BOOLEAN
VirtGpuTransferToHost2D(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONGLONG Offset)
{
    VIRTGPU_TRANSFER_TO_HOST_2D TransferRequest;

    if (!DeviceExtension->HardwareReady || DeviceExtension->CommandBusy)
        return FALSE;

    VideoPortZeroMemory(&TransferRequest, sizeof(TransferRequest));
    TransferRequest.Header.Type = VIRTGPU_CMD_TRANSFER_TO_HOST_2D;
    VirtGpuSetRectAt(&TransferRequest.Rect, X, Y, Width, Height);
    TransferRequest.Offset = Offset;
    TransferRequest.ResourceId = ResourceId;
    return VirtGpuCommandOk(DeviceExtension, &TransferRequest, sizeof(TransferRequest));
}

static BOOLEAN
VirtGpuClipFlushRect(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PLONG Left,
    _Inout_ PLONG Top,
    _Inout_ PLONG Right,
    _Inout_ PLONG Bottom)
{
    if (*Left < 0)
        *Left = 0;
    if (*Top < 0)
        *Top = 0;
    if (*Right > (LONG)DeviceExtension->ScreenWidth)
        *Right = DeviceExtension->ScreenWidth;
    if (*Bottom > (LONG)DeviceExtension->ScreenHeight)
        *Bottom = DeviceExtension->ScreenHeight;

    return (*Left < *Right) && (*Top < *Bottom);
}

static VOID
VirtGpuBuildTransferToHost2D(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Out_ PVIRTGPU_TRANSFER_TO_HOST_2D TransferRequest,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONGLONG Offset;

    Offset = ((ULONGLONG)Top * DeviceExtension->BytesPerScanLine) +
             ((ULONGLONG)Left * DeviceExtension->BytesPerPixel);

    VideoPortZeroMemory(TransferRequest, sizeof(*TransferRequest));
    TransferRequest->Header.Type = VIRTGPU_CMD_TRANSFER_TO_HOST_2D;
    VirtGpuSetRectAt(&TransferRequest->Rect, Left, Top, Width, Height);
    TransferRequest->Offset = Offset;
    TransferRequest->ResourceId = VIRTGPU_RESOURCE_ID;
}

static VOID
VirtGpuBuildResourceFlush(
    _Out_ PVIRTGPU_RESOURCE_FLUSH FlushRequest,
    _In_ ULONG ResourceId,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    VideoPortZeroMemory(FlushRequest, sizeof(*FlushRequest));
    FlushRequest->Header.Type = VIRTGPU_CMD_RESOURCE_FLUSH;
    VirtGpuSetRectAt(&FlushRequest->Rect, Left, Top, Width, Height);
    FlushRequest->ResourceId = ResourceId;
}

static BOOLEAN
VirtGpuFlushRect(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ LONG Right,
    _In_ LONG Bottom)
{
    VIRTGPU_RESOURCE_FLUSH FlushRequest;
    ULONGLONG Offset;
    ULONG Width;
    ULONG Height;

    if (!VirtGpuClipFlushRect(DeviceExtension, &Left, &Top, &Right, &Bottom))
        return TRUE;

    Width = Right - Left;
    Height = Bottom - Top;
    Offset = ((ULONGLONG)Top * DeviceExtension->BytesPerScanLine) +
             ((ULONGLONG)Left * DeviceExtension->BytesPerPixel);

    if (!VirtGpuTransferToHost2D(DeviceExtension,
                                 VIRTGPU_RESOURCE_ID,
                                 Left,
                                 Top,
                                 Width,
                                 Height,
                                 Offset))
    {
        return FALSE;
    }

    VirtGpuBuildResourceFlush(&FlushRequest,
                              VIRTGPU_RESOURCE_ID,
                              Left,
                              Top,
                              Width,
                              Height);
    return VirtGpuCommandOk(DeviceExtension, &FlushRequest, sizeof(FlushRequest));
}

static BOOLEAN
VirtGpuFlushResourceRect(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ LONG Right,
    _In_ LONG Bottom)
{
    VIRTGPU_RESOURCE_FLUSH FlushRequest;
    ULONG Width;
    ULONG Height;

    if ((ResourceId != VIRTGPU_RESOURCE_ID) &&
        (VirtGpuFindResource(DeviceExtension, ResourceId) == NULL))
    {
        return FALSE;
    }

    if (!VirtGpuClipFlushRect(DeviceExtension, &Left, &Top, &Right, &Bottom))
        return TRUE;

    Width = Right - Left;
    Height = Bottom - Top;
    VirtGpuBuildResourceFlush(&FlushRequest, ResourceId, Left, Top, Width, Height);
    return VirtGpuCommandOk(DeviceExtension, &FlushRequest, sizeof(FlushRequest));
}

static BOOLEAN
VirtGpuFlushResourceRectAsync(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ LONG Right,
    _In_ LONG Bottom)
{
    VIRTGPU_RESOURCE_FLUSH FlushRequest;
    ULONG Width;
    ULONG Height;

    if ((ResourceId != VIRTGPU_RESOURCE_ID) &&
        (VirtGpuFindResource(DeviceExtension, ResourceId) == NULL))
    {
        return FALSE;
    }

    if (!VirtGpuClipFlushRect(DeviceExtension, &Left, &Top, &Right, &Bottom))
        return TRUE;

    Width = Right - Left;
    Height = Bottom - Top;
    VirtGpuBuildResourceFlush(&FlushRequest, ResourceId, Left, Top, Width, Height);
    return VirtGpuSendCommandAsync(DeviceExtension,
                                   &FlushRequest,
                                   sizeof(FlushRequest));
}

static BOOLEAN
VirtGpuFlushRectAsync(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ LONG Right,
    _In_ LONG Bottom)
{
    VIRTGPU_TRANSFER_TO_HOST_2D TransferRequest;
    VIRTGPU_RESOURCE_FLUSH FlushRequest;
    ULONG Width;
    ULONG Height;

    if (!VirtGpuClipFlushRect(DeviceExtension, &Left, &Top, &Right, &Bottom))
        return TRUE;

    if (VirtGpuAsyncFreeCommandCount(DeviceExtension) < 2)
        return FALSE;

    Width = Right - Left;
    Height = Bottom - Top;

    VirtGpuBuildTransferToHost2D(DeviceExtension,
                                 &TransferRequest,
                                 Left,
                                 Top,
                                 Width,
                                 Height);
    VirtGpuBuildResourceFlush(&FlushRequest,
                              VIRTGPU_RESOURCE_ID,
                              Left,
                              Top,
                              Width,
                              Height);

    return VirtGpuSendCommandAsync(DeviceExtension,
                                   &TransferRequest,
                                   sizeof(TransferRequest)) &&
           VirtGpuSendCommandAsync(DeviceExtension,
                                   &FlushRequest,
                                   sizeof(FlushRequest));
}

static BOOLEAN
VirtGpuFlush(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    if (!VirtGpuFlushRect(DeviceExtension,
                          0,
                          0,
                          DeviceExtension->ScreenWidth,
                          DeviceExtension->ScreenHeight))
    {
        return FALSE;
    }

    DeviceExtension->FlushPending = FALSE;
    return TRUE;
}

static BOOLEAN
VirtGpuSendCursorCommand(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(RequestSize) PVOID Request,
    _In_ ULONG RequestSize)
{
    struct scatterlist Sg;
    PUCHAR RequestBuffer;
    unsigned int Length;
    ULONG Attempt;
    ULONG Slot;
    PVOID Used;
    BOOLEAN MoveOnly;

    MoveOnly = FALSE;
    if ((Request != NULL) && (RequestSize >= sizeof(VIRTGPU_CTRL_HDR)))
        MoveOnly = ((PVIRTGPU_CTRL_HDR)Request)->Type == VIRTGPU_CMD_MOVE_CURSOR;

    if (!DeviceExtension->CursorQueue ||
        !DeviceExtension->CursorCommandBuffer ||
        (RequestSize > VIRTGPU_CURSOR_COMMAND_BUFFER_SIZE))
    {
        return FALSE;
    }

    while ((Used = virtqueue_get_buf(DeviceExtension->CursorQueue, &Length)) != NULL)
    {
        ULONG_PTR Base = (ULONG_PTR)DeviceExtension->CursorCommandBuffer;
        ULONG_PTR Pointer = (ULONG_PTR)Used;
        ULONG Offset;

        UNREFERENCED_PARAMETER(Length);

        if ((Pointer < Base) ||
            (Pointer >= Base + VIRTGPU_CURSOR_COMMAND_BUFFER_SIZE * VIRTGPU_CURSOR_COMMAND_COUNT))
        {
            continue;
        }

        Offset = (ULONG)(Pointer - Base);
        if ((Offset % VIRTGPU_CURSOR_COMMAND_BUFFER_SIZE) == 0)
        {
            Slot = Offset / VIRTGPU_CURSOR_COMMAND_BUFFER_SIZE;
            if (Slot < VIRTGPU_CURSOR_COMMAND_COUNT)
                DeviceExtension->CursorCommandInUse[Slot] = FALSE;
        }
    }

    for (Attempt = 0; Attempt < VIRTGPU_CURSOR_COMMAND_COUNT; ++Attempt)
    {
        Slot = (DeviceExtension->CursorCommandNextSlot + Attempt) %
               VIRTGPU_CURSOR_COMMAND_COUNT;
        if (!DeviceExtension->CursorCommandInUse[Slot])
            break;
    }

    if (Attempt == VIRTGPU_CURSOR_COMMAND_COUNT)
        return MoveOnly;

    RequestBuffer = (PUCHAR)DeviceExtension->CursorCommandBuffer +
                    (Slot * VIRTGPU_CURSOR_COMMAND_BUFFER_SIZE);
    VideoPortZeroMemory(RequestBuffer, VIRTGPU_CURSOR_COMMAND_BUFFER_SIZE);
    VideoPortMoveMemory(RequestBuffer, Request, RequestSize);

    Sg.physAddr.QuadPart = DeviceExtension->CursorCommandPhysical.QuadPart +
                           (Slot * VIRTGPU_CURSOR_COMMAND_BUFFER_SIZE);
    Sg.length = RequestSize;

    if (virtqueue_add_buf(DeviceExtension->CursorQueue,
                          &Sg,
                          1,
                          0,
                          RequestBuffer,
                          NULL,
                          0) != 0)
    {
        return MoveOnly;
    }

    DeviceExtension->CursorCommandInUse[Slot] = TRUE;
    DeviceExtension->CursorCommandNextSlot =
        (Slot + 1) % VIRTGPU_CURSOR_COMMAND_COUNT;
    virtqueue_kick(DeviceExtension->CursorQueue);
    return TRUE;
}

static BOOLEAN
VirtGpuUpdateCursor(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG ResourceId)
{
    VIRTGPU_UPDATE_CURSOR Request;

    if (!DeviceExtension->CursorReady)
        return FALSE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_UPDATE_CURSOR;
    Request.Pos.ScanoutId = VIRTGPU_SCANOUT_ID;
    Request.Pos.X = DeviceExtension->CursorX;
    Request.Pos.Y = DeviceExtension->CursorY;
    Request.ResourceId = ResourceId;
    return VirtGpuSendCursorCommand(DeviceExtension, &Request, sizeof(Request));
}

static BOOLEAN
VirtGpuMoveCursor(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    VIRTGPU_UPDATE_CURSOR Request;

    if (!DeviceExtension->CursorReady || !DeviceExtension->CursorEnabled)
        return TRUE;

    VideoPortZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = VIRTGPU_CMD_MOVE_CURSOR;
    Request.Pos.ScanoutId = VIRTGPU_SCANOUT_ID;
    Request.Pos.X = DeviceExtension->CursorX;
    Request.Pos.Y = DeviceExtension->CursorY;
    Request.ResourceId = VIRTGPU_CURSOR_RESOURCE_ID;
    return VirtGpuSendCursorCommand(DeviceExtension, &Request, sizeof(Request));
}

static VOID
VirtGpuSetCursorPosition(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ SHORT Column,
    _In_ SHORT Row)
{
    DeviceExtension->CursorX = (Column < 0) ? 0 : (ULONG)Column;
    DeviceExtension->CursorY = (Row < 0) ? 0 : (ULONG)Row;
}

static BOOLEAN
VirtGpuSetPointerAttributes(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(AttributesSize) PVIDEO_POINTER_ATTRIBUTES Attributes,
    _In_ ULONG AttributesSize)
{
    PUCHAR Source;
    PUCHAR Destination;
    ULONG RequiredSize;
    ULONG Row;
    ULONG CopyBytes;

    if (!DeviceExtension->CursorReady ||
        (AttributesSize < offsetof(VIDEO_POINTER_ATTRIBUTES, Pixels)) ||
        !(Attributes->Flags & VIDEO_MODE_COLOR_POINTER) ||
        (Attributes->Width == 0) ||
        (Attributes->Height == 0) ||
        (Attributes->Width > VIRTGPU_CURSOR_WIDTH) ||
        (Attributes->Height > VIRTGPU_CURSOR_HEIGHT) ||
        (Attributes->WidthInBytes < Attributes->Width * sizeof(ULONG)))
    {
        return FALSE;
    }

    if (Attributes->Height >
        (MAXULONG - offsetof(VIDEO_POINTER_ATTRIBUTES, Pixels)) / Attributes->WidthInBytes)
    {
        return FALSE;
    }

    RequiredSize = offsetof(VIDEO_POINTER_ATTRIBUTES, Pixels) +
                   Attributes->WidthInBytes * Attributes->Height;
    if (AttributesSize < RequiredSize)
        return FALSE;

    VideoPortZeroMemory(DeviceExtension->CursorBufferVirtual,
                        DeviceExtension->CursorBufferSize);

    Source = Attributes->Pixels;
    Destination = DeviceExtension->CursorBufferVirtual;
    CopyBytes = Attributes->Width * sizeof(ULONG);

    for (Row = 0; Row < Attributes->Height; ++Row)
    {
        VideoPortMoveMemory(Destination + (Row * VIRTGPU_CURSOR_WIDTH * sizeof(ULONG)),
                            Source + (Row * Attributes->WidthInBytes),
                            CopyBytes);
    }

    DeviceExtension->CursorWidth = Attributes->Width;
    DeviceExtension->CursorHeight = Attributes->Height;
    VirtGpuSetCursorPosition(DeviceExtension, Attributes->Column, Attributes->Row);

    if (!VirtGpuTransferToHost2D(DeviceExtension,
                                 VIRTGPU_CURSOR_RESOURCE_ID,
                                 0,
                                 0,
                                 VIRTGPU_CURSOR_WIDTH,
                                 VIRTGPU_CURSOR_HEIGHT,
                                 0))
    {
        return FALSE;
    }

    DeviceExtension->CursorShapeValid = TRUE;
    DeviceExtension->CursorEnabled = (Attributes->Enable != 0);

    if (DeviceExtension->CursorEnabled)
        return VirtGpuUpdateCursor(DeviceExtension, VIRTGPU_CURSOR_RESOURCE_ID);

    return VirtGpuUpdateCursor(DeviceExtension, 0);
}

static BOOLEAN
VirtGpuEnableCursor(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    if (!DeviceExtension->CursorReady || !DeviceExtension->CursorShapeValid)
        return FALSE;

    DeviceExtension->CursorEnabled = TRUE;
    return VirtGpuUpdateCursor(DeviceExtension, VIRTGPU_CURSOR_RESOURCE_ID);
}

static BOOLEAN
VirtGpuDisableCursor(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    if (!DeviceExtension->CursorReady)
        return TRUE;

    DeviceExtension->CursorEnabled = FALSE;
    return VirtGpuUpdateCursor(DeviceExtension, 0);
}

static BOOLEAN
VirtGpuCreateCursorResource(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    struct
    {
        VIRTGPU_RESOURCE_ATTACH_BACKING Attach;
        VIRTGPU_MEM_ENTRY Entry;
    } AttachRequest;
    VIRTGPU_RESOURCE_CREATE_2D CreateRequest;

    if (DeviceExtension->CursorQueue == NULL)
        return FALSE;

    DeviceExtension->CursorBufferSize =
        VIRTGPU_CURSOR_WIDTH * VIRTGPU_CURSOR_HEIGHT * sizeof(ULONG);
    DeviceExtension->CursorBufferVirtual =
        VirtGpuAllocateContiguous(DeviceExtension,
                                  DeviceExtension->CursorBufferSize,
                                  &DeviceExtension->CursorBufferPhysical);
    if (DeviceExtension->CursorBufferVirtual == NULL)
        return FALSE;

    DeviceExtension->CursorCommandBuffer =
        VirtGpuAllocateContiguous(DeviceExtension,
                                  VIRTGPU_CURSOR_COMMAND_BUFFER_SIZE *
                                  VIRTGPU_CURSOR_COMMAND_COUNT,
                                  &DeviceExtension->CursorCommandPhysical);
    if (DeviceExtension->CursorCommandBuffer == NULL)
        return FALSE;

    VideoPortZeroMemory(&CreateRequest, sizeof(CreateRequest));
    CreateRequest.Header.Type = VIRTGPU_CMD_RESOURCE_CREATE_2D;
    CreateRequest.ResourceId = VIRTGPU_CURSOR_RESOURCE_ID;
    CreateRequest.Format = VIRTGPU_FORMAT_B8G8R8A8_UNORM;
    CreateRequest.Width = VIRTGPU_CURSOR_WIDTH;
    CreateRequest.Height = VIRTGPU_CURSOR_HEIGHT;
    if (!VirtGpuCommandOk(DeviceExtension, &CreateRequest, sizeof(CreateRequest)))
        return FALSE;

    VideoPortZeroMemory(&AttachRequest, sizeof(AttachRequest));
    AttachRequest.Attach.Header.Type = VIRTGPU_CMD_RESOURCE_ATTACH_BACKING;
    AttachRequest.Attach.ResourceId = VIRTGPU_CURSOR_RESOURCE_ID;
    AttachRequest.Attach.EntryCount = 1;
    AttachRequest.Entry.Address = DeviceExtension->CursorBufferPhysical.QuadPart;
    AttachRequest.Entry.Length = DeviceExtension->CursorBufferSize;
    if (!VirtGpuCommandOk(DeviceExtension, &AttachRequest, sizeof(AttachRequest)))
        return FALSE;

    DeviceExtension->CursorReady = TRUE;
    return TRUE;
}

typedef struct _VIRTGPU_MODE_SIZE
{
    ULONG Width;
    ULONG Height;
} VIRTGPU_MODE_SIZE, *PVIRTGPU_MODE_SIZE;

static VOID
VirtGpuFillModeInfo(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _Out_ PVIDEO_MODE_INFORMATION Mode,
    _In_ ULONG ModeIndex,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    const ULONG Dpi = 96;

    VideoPortZeroMemory(Mode, sizeof(*Mode));

    Mode->Length = sizeof(*Mode);
    Mode->ModeIndex = ModeIndex;
    Mode->VisScreenWidth = Width;
    Mode->VisScreenHeight = Height;
    Mode->ScreenStride = DeviceExtension->BytesPerScanLine;
    Mode->NumberOfPlanes = 1;
    Mode->BitsPerPlane = 32;
    Mode->Frequency = 60;
    Mode->XMillimeter =
        ((ULONGLONG)Width * 254 + (Dpi * 5)) / (Dpi * 10);
    Mode->YMillimeter =
        ((ULONGLONG)Height * 254 + (Dpi * 5)) / (Dpi * 10);
    Mode->NumberRedBits = 8;
    Mode->NumberGreenBits = 8;
    Mode->NumberBlueBits = 8;
    Mode->RedMask = 0x00FF0000;
    Mode->GreenMask = 0x0000FF00;
    Mode->BlueMask = 0x000000FF;
    Mode->AttributeFlags = VIDEO_MODE_GRAPHICS |
                           VIDEO_MODE_COLOR |
                           VIDEO_MODE_LINEAR |
                           VIDEO_MODE_NO_OFF_SCREEN;
    Mode->VideoMemoryBitmapWidth = Width;
    Mode->VideoMemoryBitmapHeight = Height;
}

static BOOLEAN
VirtGpuHasMode(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG Index;

    for (Index = 0; Index < DeviceExtension->ModeCount; ++Index)
    {
        if ((DeviceExtension->ModeInfo[Index].VisScreenWidth == Width) &&
            (DeviceExtension->ModeInfo[Index].VisScreenHeight == Height))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static VOID
VirtGpuAddMode(
    _Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG Index;

    if ((Width == 0) ||
        (Height == 0) ||
        (Width < VIRTGPU_MIN_VIDEO_WIDTH) ||
        (Height < VIRTGPU_MIN_VIDEO_HEIGHT) ||
        (Width > DeviceExtension->MaxScreenWidth) ||
        (Height > DeviceExtension->MaxScreenHeight) ||
        (DeviceExtension->ModeCount >= VIRTGPU_MAX_VIDEO_MODES) ||
        VirtGpuHasMode(DeviceExtension, Width, Height))
    {
        return;
    }

    Index = DeviceExtension->ModeCount++;
    VirtGpuFillModeInfo(DeviceExtension,
                        &DeviceExtension->ModeInfo[Index],
                        Index,
                        Width,
                        Height);
}

static VOID
VirtGpuAddEdidEstablishedModes(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    const UCHAR* Edid = DeviceExtension->Edid;

    if (!DeviceExtension->EdidValid || (DeviceExtension->EdidSize < 0x26))
        return;

    if (Edid[0x23] & 0xC0)
        VirtGpuAddMode(DeviceExtension, 720, 400);
    if (Edid[0x23] & 0x3C)
        VirtGpuAddMode(DeviceExtension, 640, 480);
    if (Edid[0x23] & 0x03)
        VirtGpuAddMode(DeviceExtension, 800, 600);

    if (Edid[0x24] & 0xC0)
        VirtGpuAddMode(DeviceExtension, 800, 600);
    if (Edid[0x24] & 0x20)
        VirtGpuAddMode(DeviceExtension, 832, 624);
    if (Edid[0x24] & 0x1E)
        VirtGpuAddMode(DeviceExtension, 1024, 768);
    if (Edid[0x24] & 0x01)
        VirtGpuAddMode(DeviceExtension, 1280, 1024);

    if (Edid[0x25] & 0x80)
        VirtGpuAddMode(DeviceExtension, 1152, 870);
}

static VOID
VirtGpuAddEdidStandardModes(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    const UCHAR* Edid = DeviceExtension->Edid;
    ULONG Offset;

    if (!DeviceExtension->EdidValid || (DeviceExtension->EdidSize < 0x36))
        return;

    for (Offset = 0x26; Offset < 0x36; Offset += 2)
    {
        ULONG Width;
        ULONG Height;

        if ((Edid[Offset] == 0x01) && (Edid[Offset + 1] == 0x01))
            continue;

        Width = ((ULONG)Edid[Offset] + 31) * 8;
        switch (Edid[Offset + 1] >> 6)
        {
            case 0:
                Height = (Width / 16) * 10;
                break;
            case 1:
                Height = (Width / 4) * 3;
                break;
            case 2:
                Height = (Width / 5) * 4;
                break;
            default:
                Height = (Width / 16) * 9;
                break;
        }

        VirtGpuAddMode(DeviceExtension, Width, Height);
    }
}

static VOID
VirtGpuAddEdidDetailedModes(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    const UCHAR* Edid = DeviceExtension->Edid;
    ULONG Offset;

    if (!DeviceExtension->EdidValid || (DeviceExtension->EdidSize < 0x7E))
        return;

    for (Offset = 0x36; Offset < 0x7E; Offset += 18)
    {
        const UCHAR* Descriptor = &Edid[Offset];
        ULONG PixelClock = Descriptor[0] | ((ULONG)Descriptor[1] << 8);
        ULONG Width;
        ULONG Height;

        if (PixelClock == 0)
            continue;

        Width = Descriptor[2] | ((ULONG)(Descriptor[4] & 0xF0) << 4);
        Height = Descriptor[5] | ((ULONG)(Descriptor[7] & 0xF0) << 4);
        VirtGpuAddMode(DeviceExtension, Width, Height);
    }
}

static VOID
VirtGpuAddEdidModes(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    VirtGpuAddEdidEstablishedModes(DeviceExtension);
    VirtGpuAddEdidStandardModes(DeviceExtension);
    VirtGpuAddEdidDetailedModes(DeviceExtension);
}

static VOID
VirtGpuInitializeModeInfo(_Inout_ PVIRTGPU_DEVICE_EXTENSION DeviceExtension)
{
    static const VIRTGPU_MODE_SIZE StandardModes[] =
    {
        { 720, 400 },
        { 640, 480 },
        { 800, 600 },
        { 832, 624 },
        { 848, 480 },
        { 1024, 768 },
        { 1152, 864 },
        { 1152, 870 },
        { 1280, 720 },
        { 1280, 768 },
        { 1280, 800 },
        { 1280, 960 },
        { 1280, 1024 },
        { 1360, 768 },
        { 1366, 768 },
        { 1400, 1050 },
        { 1440, 900 },
        { 1600, 900 },
        { 1600, 1200 },
        { 1680, 1050 },
        { 1792, 1344 },
        { 1856, 1392 },
        { 1920, 1080 },
        { 1920, 1200 },
        { 1920, 1440 },
        { 2560, 1440 }
    };
    ULONG Index;

    DeviceExtension->ModeCount = 0;
    VideoPortZeroMemory(DeviceExtension->ModeInfo, sizeof(DeviceExtension->ModeInfo));

    VirtGpuAddMode(DeviceExtension,
                   DeviceExtension->MaxScreenWidth,
                   DeviceExtension->MaxScreenHeight);
    VirtGpuAddEdidModes(DeviceExtension);

    for (Index = 0; Index < sizeof(StandardModes) / sizeof(StandardModes[0]); ++Index)
    {
        VirtGpuAddMode(DeviceExtension,
                       StandardModes[Index].Width,
                       StandardModes[Index].Height);
    }

    if (DeviceExtension->ModeCount == 0)
    {
        VirtGpuAddMode(DeviceExtension, 640, 480);
    }

    DeviceExtension->CurrentMode = 0;
    DeviceExtension->ScreenWidth = DeviceExtension->ModeInfo[0].VisScreenWidth;
    DeviceExtension->ScreenHeight = DeviceExtension->ModeInfo[0].VisScreenHeight;
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
    InitData.AdapterInterfaceType = PCIBus;
    InitData.HwFindAdapter = VirtGpuFindAdapter;
    InitData.HwInitialize = VirtGpuInitialize;
    InitData.HwStartIO = VirtGpuStartIO;
    InitData.HwResetHw = VirtGpuResetHw;
    InitData.HwTimer = VirtGpuTimer;
    InitData.HwGetPowerState = VirtGpuGetPowerState;
    InitData.HwSetPowerState = VirtGpuSetPowerState;
    InitData.HwGetVideoChildDescriptor = VirtGpuGetVideoChildDescriptor;
    InitData.HwDeviceExtensionSize = sizeof(VIRTGPU_DEVICE_EXTENSION);
    InitData.StartingDeviceNumber = 0;

    return VideoPortInitialize(Context1, Context2, &InitData, NULL);
}

VP_STATUS
NTAPI
VirtGpuFindAdapter(
    _In_ PVOID HwDeviceExtension,
    _In_ PVOID HwContext,
    _In_ PWSTR ArgumentString,
    _Inout_ PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    _Out_ PUCHAR Again)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;
    VideoPortZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    if (ConfigInfo->Length < sizeof(*ConfigInfo))
        return ERROR_INVALID_PARAMETER;

    if (!VirtGpuFindPciDevice(DeviceExtension, ConfigInfo))
        return ERROR_DEV_NOT_EXIST;

    VirtGpuEnablePciDevice(DeviceExtension);
    ConfigInfo->NumEmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntries = 0;
    ConfigInfo->EmulatorAccessEntriesContext = 0;
    ConfigInfo->HardwareStateSize = 0;

    return NO_ERROR;
}

BOOLEAN
NTAPI
VirtGpuInitialize(_In_ PVOID HwDeviceExtension)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    struct virtqueue* Queues[2];
    ULONGLONG FrameBufferSize;
    ULONGLONG Features;
    NTSTATUS Status;

    Status = virtio_device_initialize(&DeviceExtension->VirtIODevice,
                                      &VirtGpuSystemOps,
                                      DeviceExtension,
                                      FALSE);
    if (!NT_SUCCESS(Status))
    {
        VideoDebugPrint((Error, "VirtGpu: virtio init failed 0x%lx\n", Status));
        return FALSE;
    }

    DeviceExtension->HostFeatures =
        virtio_get_features(&DeviceExtension->VirtIODevice);
    Features = DeviceExtension->HostFeatures & (1ULL << VIRTIO_F_VERSION_1);
    if (DeviceExtension->HostFeatures & (1ULL << VIRTGPU_F_VIRGL))
        Features |= (1ULL << VIRTGPU_F_VIRGL);
    if (DeviceExtension->HostFeatures & (1ULL << VIRTGPU_F_EDID))
        Features |= (1ULL << VIRTGPU_F_EDID);
    if (DeviceExtension->HostFeatures & (1ULL << VIRTGPU_F_RESOURCE_UUID))
        Features |= (1ULL << VIRTGPU_F_RESOURCE_UUID);
    if (DeviceExtension->HostFeatures & (1ULL << VIRTGPU_F_RESOURCE_BLOB))
        Features |= (1ULL << VIRTGPU_F_RESOURCE_BLOB);
    if (DeviceExtension->HostFeatures & (1ULL << VIRTGPU_F_CONTEXT_INIT))
        Features |= (1ULL << VIRTGPU_F_CONTEXT_INIT);

    Status = virtio_set_features(&DeviceExtension->VirtIODevice, Features);
    if (!NT_SUCCESS(Status))
    {
        VideoDebugPrint((Error, "VirtGpu: feature negotiation failed 0x%lx\n", Status));
        return FALSE;
    }
    DeviceExtension->GuestFeatures = Features;
    DeviceExtension->VirglSupported =
        (Features & (1ULL << VIRTGPU_F_VIRGL)) != 0;
    DeviceExtension->EdidSupported =
        (Features & (1ULL << VIRTGPU_F_EDID)) != 0;
    DeviceExtension->ResourceUuidSupported =
        (Features & (1ULL << VIRTGPU_F_RESOURCE_UUID)) != 0;
    DeviceExtension->ResourceBlobSupported =
        (Features & (1ULL << VIRTGPU_F_RESOURCE_BLOB)) != 0;
    DeviceExtension->ContextInitSupported =
        (Features & (1ULL << VIRTGPU_F_CONTEXT_INIT)) != 0;
    DeviceExtension->NextContextId = 1;
    DeviceExtension->NextResourceId = VIRTGPU_FIRST_3D_RESOURCE_ID;
    DeviceExtension->NextFenceId = 0;
    DeviceExtension->CompletedFenceId = 0;

    Status = virtio_find_queues(&DeviceExtension->VirtIODevice, 2, Queues);
    if (!NT_SUCCESS(Status))
    {
        VideoDebugPrint((Error, "VirtGpu: queue setup failed 0x%lx\n", Status));
        return FALSE;
    }
    DeviceExtension->ControlQueue = Queues[0];
    DeviceExtension->CursorQueue = Queues[1];

    DeviceExtension->CommandBuffer =
        VirtGpuAllocateContiguous(DeviceExtension,
                                  VIRTGPU_COMMAND_BUFFER_SIZE,
                                  &DeviceExtension->CommandPhysical);
    if (DeviceExtension->CommandBuffer == NULL)
        return FALSE;

    DeviceExtension->AsyncCommandBuffer =
        VirtGpuAllocateContiguous(DeviceExtension,
                                  VIRTGPU_ASYNC_COMMAND_COUNT *
                                      VIRTGPU_ASYNC_COMMAND_SLOT_SIZE,
                                  &DeviceExtension->AsyncCommandPhysical);
    if (DeviceExtension->AsyncCommandBuffer == NULL)
    {
        VideoDebugPrint((Warn,
            "VirtGpu: async 2D command queue unavailable, using synchronous flushes\n"));
    }

    virtio_device_ready(&DeviceExtension->VirtIODevice);

    VirtGpuQuery3DCapsets(DeviceExtension);
    VirtGpuQueryEdid(DeviceExtension);

    if (!VirtGpuGetDisplayInfo(DeviceExtension,
                               &DeviceExtension->MaxScreenWidth,
                               &DeviceExtension->MaxScreenHeight))
    {
        VideoDebugPrint((Warn, "VirtGpu: display info unavailable, using 1280x960\n"));
        DeviceExtension->MaxScreenWidth = 1280;
        DeviceExtension->MaxScreenHeight = 960;
    }

    DeviceExtension->BytesPerPixel = sizeof(ULONG);
    DeviceExtension->PixelsPerScanLine = DeviceExtension->MaxScreenWidth;
    DeviceExtension->BytesPerScanLine =
        DeviceExtension->PixelsPerScanLine * DeviceExtension->BytesPerPixel;
    FrameBufferSize =
        (ULONGLONG)DeviceExtension->BytesPerScanLine *
        DeviceExtension->MaxScreenHeight;
    if ((FrameBufferSize == 0) || (FrameBufferSize > MAXULONG))
        return FALSE;

    DeviceExtension->FrameBufferSize = (ULONG)FrameBufferSize;
    DeviceExtension->FrameBufferVirtual =
        VirtGpuAllocateContiguous(DeviceExtension,
                                  DeviceExtension->FrameBufferSize,
                                  &DeviceExtension->FrameBufferPhysical);
    if (DeviceExtension->FrameBufferVirtual == NULL)
        return FALSE;

    VirtGpuInitializeModeInfo(DeviceExtension);

    if (!VirtGpuCreate2DResource(DeviceExtension))
    {
        VideoDebugPrint((Error, "VirtGpu: failed to create scanout resource\n"));
        return FALSE;
    }

    if (!VirtGpuCreateCursorResource(DeviceExtension))
    {
        VideoDebugPrint((Warn, "VirtGpu: failed to create cursor resource\n"));
    }

    DeviceExtension->HardwareReady = TRUE;
    DeviceExtension->FlushPending = TRUE;
    if (!VirtGpuFlush(DeviceExtension))
        VideoPortStartTimer(DeviceExtension);

    VideoDebugPrint((Info,
        "VirtGpu: initialized %lux%lu at PA 0x%I64x (%lu bytes), 3D=%u capsets=%lu preferred=%lu blob=%u uuid=%u edid=%u\n",
        DeviceExtension->ScreenWidth,
        DeviceExtension->ScreenHeight,
        DeviceExtension->FrameBufferPhysical.QuadPart,
        DeviceExtension->FrameBufferSize,
        DeviceExtension->VirglSupported ? 1 : 0,
        DeviceExtension->CapsetCount,
        DeviceExtension->PreferredCapsetId,
        DeviceExtension->ResourceBlobSupported ? 1 : 0,
        DeviceExtension->ResourceUuidSupported ? 1 : 0,
        DeviceExtension->EdidValid ? 1 : 0));
    return TRUE;
}

BOOLEAN
NTAPI
VirtGpuStartIO(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_REQUEST_PACKET RequestPacket)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    VP_STATUS Status = ERROR_INVALID_FUNCTION;
    PVIDEO_NUM_MODES NumModes;
    PVIDEO_MODE_INFORMATION ModeInfo;
    PVIDEO_MODE VideoMode;
    PVIDEO_MEMORY VideoMemory;
    PVIDEO_MEMORY_INFORMATION MemoryInfo;
    PVIDEO_POINTER_CAPABILITIES PointerCaps;
    PVIDEO_POINTER_ATTRIBUTES PointerAttributes;
    PVIDEO_POINTER_POSITION PointerPosition;
    PVIRTGPU_DIRTY_RECT DirtyRect;
    PVIRTGPU_SCANOUT_RESOURCE ScanoutResource;
    PVIRTGPU_RESOURCE_DIRTY_RECT ResourceDirtyRect;
    ULONG InIoSpace;

    switch (RequestPacket->IoControlCode)
    {
        case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES:
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_NUM_MODES))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            NumModes = RequestPacket->OutputBuffer;
            NumModes->NumModes = DeviceExtension->ModeCount;
            NumModes->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);
            RequestPacket->StatusBlock->Information = sizeof(VIDEO_NUM_MODES);
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_QUERY_AVAIL_MODES:
            if (RequestPacket->OutputBufferLength <
                DeviceExtension->ModeCount * sizeof(VIDEO_MODE_INFORMATION))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            ModeInfo = RequestPacket->OutputBuffer;
            VideoPortMoveMemory(ModeInfo,
                                DeviceExtension->ModeInfo,
                                DeviceExtension->ModeCount * sizeof(VIDEO_MODE_INFORMATION));
            RequestPacket->StatusBlock->Information =
                DeviceExtension->ModeCount * sizeof(VIDEO_MODE_INFORMATION);
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_QUERY_CURRENT_MODE:
            if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            ModeInfo = RequestPacket->OutputBuffer;
            VideoPortMoveMemory(ModeInfo,
                                &DeviceExtension->ModeInfo[DeviceExtension->CurrentMode],
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
            VideoMode = RequestPacket->InputBuffer;
            {
                ULONG RequestedMode = VideoMode->RequestedMode &
                                      ~(VIDEO_MODE_NO_ZERO_MEMORY |
                                        VIDEO_MODE_MAP_MEM_LINEAR);
                ULONG OldMode = DeviceExtension->CurrentMode;
                ULONG OldWidth = DeviceExtension->ScreenWidth;
                ULONG OldHeight = DeviceExtension->ScreenHeight;

                if (RequestedMode >= DeviceExtension->ModeCount)
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                DeviceExtension->CurrentMode = RequestedMode;
                DeviceExtension->ScreenWidth =
                    DeviceExtension->ModeInfo[RequestedMode].VisScreenWidth;
                DeviceExtension->ScreenHeight =
                    DeviceExtension->ModeInfo[RequestedMode].VisScreenHeight;

                if (!VirtGpuSetScanout(DeviceExtension))
                {
                    DeviceExtension->CurrentMode = OldMode;
                    DeviceExtension->ScreenWidth = OldWidth;
                    DeviceExtension->ScreenHeight = OldHeight;
                    VirtGpuSetScanout(DeviceExtension);
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }
            }

            if (!(VideoMode->RequestedMode & VIDEO_MODE_NO_ZERO_MEMORY))
            {
                VideoPortZeroMemory(DeviceExtension->FrameBufferVirtual,
                                    DeviceExtension->FrameBufferSize);
            }

            DeviceExtension->FlushPending = TRUE;
            if (!VirtGpuFlush(DeviceExtension))
                VideoPortStartTimer(DeviceExtension);
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

            VideoMemory = RequestPacket->InputBuffer;
            MemoryInfo = RequestPacket->OutputBuffer;
            MemoryInfo->VideoRamBase = VideoMemory->RequestedVirtualAddress;
            MemoryInfo->VideoRamLength = DeviceExtension->FrameBufferSize;
            InIoSpace = VIDEO_MEMORY_SPACE_MEMORY;
            Status = VideoPortMapMemory(DeviceExtension,
                                        DeviceExtension->FrameBufferPhysical,
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
                DeviceExtension->FlushPending = TRUE;
                if (!VirtGpuFlush(DeviceExtension))
                    VideoPortStartTimer(DeviceExtension);
            }
            break;

        case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            VideoMemory = RequestPacket->InputBuffer;
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
            PointerCaps = RequestPacket->OutputBuffer;
            VideoPortZeroMemory(PointerCaps, sizeof(*PointerCaps));
            if (DeviceExtension->CursorReady)
            {
                PointerCaps->Flags = VIDEO_MODE_COLOR_POINTER |
                                     VIDEO_MODE_ASYNC_POINTER;
                PointerCaps->MaxWidth = VIRTGPU_CURSOR_WIDTH;
                PointerCaps->MaxHeight = VIRTGPU_CURSOR_HEIGHT;
            }
            RequestPacket->StatusBlock->Information =
                sizeof(VIDEO_POINTER_CAPABILITIES);
            Status = NO_ERROR;
            break;

        case IOCTL_VIDEO_SET_POINTER_ATTR:
            if (RequestPacket->InputBufferLength <
                offsetof(VIDEO_POINTER_ATTRIBUTES, Pixels))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            PointerAttributes = RequestPacket->InputBuffer;
            Status = VirtGpuSetPointerAttributes(DeviceExtension,
                                                 PointerAttributes,
                                                 RequestPacket->InputBufferLength) ?
                     NO_ERROR : ERROR_INVALID_PARAMETER;
            break;

        case IOCTL_VIDEO_SET_POINTER_POSITION:
            if (RequestPacket->InputBufferLength < sizeof(VIDEO_POINTER_POSITION))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            PointerPosition = RequestPacket->InputBuffer;
            VirtGpuSetCursorPosition(DeviceExtension,
                                     PointerPosition->Column,
                                     PointerPosition->Row);
            Status = VirtGpuMoveCursor(DeviceExtension) ?
                     NO_ERROR : ERROR_INVALID_PARAMETER;
            break;

        case IOCTL_VIDEO_ENABLE_POINTER:
            Status = VirtGpuEnableCursor(DeviceExtension) ?
                     NO_ERROR : ERROR_INVALID_PARAMETER;
            break;

        case IOCTL_VIDEO_DISABLE_POINTER:
            Status = VirtGpuDisableCursor(DeviceExtension) ?
                     NO_ERROR : ERROR_INVALID_PARAMETER;
            break;

        case IOCTL_VIDEO_VIRTGPU_FLUSH_RECT:
            if (RequestPacket->InputBufferLength < sizeof(VIRTGPU_DIRTY_RECT))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            DirtyRect = RequestPacket->InputBuffer;
            Status = (VirtGpuFlushRectAsync(DeviceExtension,
                                            DirtyRect->Left,
                                            DirtyRect->Top,
                                            DirtyRect->Right,
                                            DirtyRect->Bottom) ||
                      VirtGpuFlushRect(DeviceExtension,
                                       DirtyRect->Left,
                                       DirtyRect->Top,
                                       DirtyRect->Right,
                                       DirtyRect->Bottom)) ?
                     NO_ERROR : ERROR_INVALID_PARAMETER;
            break;

        case IOCTL_VIDEO_VIRTGPU_SET_SCANOUT_RESOURCE:
            if (RequestPacket->InputBufferLength < sizeof(VIRTGPU_SCANOUT_RESOURCE))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            ScanoutResource = RequestPacket->InputBuffer;
            Status = VirtGpuSetScanoutResource(DeviceExtension,
                                               ScanoutResource->ResourceId,
                                               ScanoutResource->X,
                                               ScanoutResource->Y,
                                               ScanoutResource->Width,
                                               ScanoutResource->Height) ?
                     NO_ERROR : ERROR_INVALID_PARAMETER;
            break;

        case IOCTL_VIDEO_VIRTGPU_RESOURCE_FLUSH_RECT:
            if (RequestPacket->InputBufferLength < sizeof(VIRTGPU_RESOURCE_DIRTY_RECT))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            ResourceDirtyRect = RequestPacket->InputBuffer;
            Status = (VirtGpuFlushResourceRectAsync(DeviceExtension,
                                                    ResourceDirtyRect->ResourceId,
                                                    ResourceDirtyRect->Left,
                                                    ResourceDirtyRect->Top,
                                                    ResourceDirtyRect->Right,
                                                    ResourceDirtyRect->Bottom) ||
                      VirtGpuFlushResourceRect(DeviceExtension,
                                               ResourceDirtyRect->ResourceId,
                                               ResourceDirtyRect->Left,
                                               ResourceDirtyRect->Top,
                                               ResourceDirtyRect->Right,
                                               ResourceDirtyRect->Bottom)) ?
                     NO_ERROR : ERROR_INVALID_PARAMETER;
            break;

        case IOCTL_VIDEO_VIRTGPU_QUERY_3D_CAPS:
            if (RequestPacket->OutputBufferLength < sizeof(VIRTGPU_3D_CAPS))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_CAPS Caps = RequestPacket->OutputBuffer;
                ULONG Index;

                VideoPortZeroMemory(Caps, sizeof(*Caps));
                Caps->Size = sizeof(*Caps);
                Caps->Enabled = DeviceExtension->VirglSupported ? 1 : 0;
                Caps->ContextInitSupported =
                    DeviceExtension->ContextInitSupported ? 1 : 0;
                Caps->ResourceUuidSupported =
                    DeviceExtension->ResourceUuidSupported ? 1 : 0;
                Caps->ResourceBlobSupported =
                    DeviceExtension->ResourceBlobSupported ? 1 : 0;
                Caps->NumCapsets = DeviceExtension->CapsetCount;
                Caps->SupportedCapsetMask = DeviceExtension->SupportedCapsetMask;
                Caps->PreferredCapsetId = DeviceExtension->PreferredCapsetId;
                Caps->PreferredCapsetVersion =
                    DeviceExtension->PreferredCapsetVersion;
                Caps->MaxCommandBytes =
                    VIRTGPU_RESPONSE_OFFSET - sizeof(VIRTGPU_SUBMIT_3D);
                Caps->BatchSupported = DeviceExtension->VirglSupported ? 1 : 0;
                Caps->MaxBatchBytes = DeviceExtension->VirglSupported ?
                    VIRTGPU_3D_MAX_BATCH_BYTES : 0;
                Caps->MaxBatchCommands = DeviceExtension->VirglSupported ?
                    VIRTGPU_3D_MAX_BATCH_COMMANDS : 0;
                Caps->LastCompletedFenceId = DeviceExtension->CompletedFenceId;
                Caps->HostFeatures = DeviceExtension->HostFeatures;
                Caps->GuestFeatures = DeviceExtension->GuestFeatures;

                for (Index = 0; Index < DeviceExtension->CapsetCount; ++Index)
                    Caps->Capsets[Index] = DeviceExtension->Capsets[Index];

                RequestPacket->StatusBlock->Information = sizeof(*Caps);
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_GET_CAPSET:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if ((RequestPacket->InputBufferLength <
                 offsetof(VIRTGPU_3D_GET_CAPSET, Data)) ||
                (RequestPacket->OutputBufferLength <
                 offsetof(VIRTGPU_3D_GET_CAPSET, Data)))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_GET_CAPSET Input = RequestPacket->InputBuffer;
                PVIRTGPU_3D_GET_CAPSET Output = RequestPacket->OutputBuffer;
                PVIRTGPU_3D_CAPSET_INFO Capset;
                ULONG HeaderSize = offsetof(VIRTGPU_3D_GET_CAPSET, Data);
                ULONG Capacity = RequestPacket->OutputBufferLength - HeaderSize;
                ULONG Version;
                ULONG Size;

                Capset = VirtGpuFindCapset(DeviceExtension, Input->CapsetId);
                if (Capset == NULL)
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                Version = Input->CapsetVersion;
                if (Version == 0)
                    Version = Capset->MaxVersion;
                if (Version > Capset->MaxVersion)
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                Size = Input->Size;
                if ((Size == 0) || (Size > Capacity))
                    Size = Capacity;
                if (Size > Capset->MaxSize)
                    Size = Capset->MaxSize;
                if (Size == 0)
                {
                    Status = ERROR_INSUFFICIENT_BUFFER;
                    break;
                }

                if (!VirtGpuGetCapset(DeviceExtension,
                                      Capset->CapsetId,
                                      Version,
                                      Output->Data,
                                      Size))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                Output->CapsetId = Capset->CapsetId;
                Output->CapsetVersion = Version;
                Output->Size = Size;
                RequestPacket->StatusBlock->Information = HeaderSize + Size;
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_CREATE_CONTEXT:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if ((RequestPacket->InputBufferLength < sizeof(VIRTGPU_3D_CREATE_CONTEXT)) ||
                (RequestPacket->OutputBufferLength < sizeof(VIRTGPU_3D_CREATE_CONTEXT)))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                VIRTGPU_3D_CREATE_CONTEXT Context;

                VideoPortMoveMemory(&Context,
                                    RequestPacket->InputBuffer,
                                    sizeof(Context));
                if (!VirtGpuCreateContext(DeviceExtension,
                                          &Context.ContextId,
                                          Context.CapsetId,
                                          Context.ContextInit,
                                          Context.DebugName))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                VideoPortMoveMemory(RequestPacket->OutputBuffer,
                                    &Context,
                                    sizeof(Context));
                RequestPacket->StatusBlock->Information = sizeof(Context);
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_DESTROY_CONTEXT:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if (RequestPacket->InputBufferLength < sizeof(VIRTGPU_3D_CONTEXT))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_CONTEXT Context = RequestPacket->InputBuffer;

                Status = VirtGpuDestroyContext(DeviceExtension, Context->ContextId) ?
                         NO_ERROR : ERROR_INVALID_PARAMETER;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_CREATE_RESOURCE:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if ((RequestPacket->InputBufferLength < sizeof(VIRTGPU_3D_CREATE_RESOURCE)) ||
                (RequestPacket->OutputBufferLength < sizeof(VIRTGPU_3D_CREATE_RESOURCE)))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                VIRTGPU_3D_CREATE_RESOURCE Resource;

                VideoPortMoveMemory(&Resource,
                                    RequestPacket->InputBuffer,
                                    sizeof(Resource));
                if (!VirtGpuCreate3DResource(DeviceExtension, &Resource))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                VideoPortMoveMemory(RequestPacket->OutputBuffer,
                                    &Resource,
                                    sizeof(Resource));
                RequestPacket->StatusBlock->Information = sizeof(Resource);
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_CREATE_BLOB:
            if (!DeviceExtension->ResourceBlobSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if ((RequestPacket->InputBufferLength <
                 offsetof(VIRTGPU_3D_CREATE_BLOB, Commands)) ||
                (RequestPacket->OutputBufferLength <
                 offsetof(VIRTGPU_3D_CREATE_BLOB, Commands)))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                VIRTGPU_3D_CREATE_BLOB Blob;
                ULONG HeaderSize = offsetof(VIRTGPU_3D_CREATE_BLOB, Commands);

                VideoPortMoveMemory(&Blob,
                                    RequestPacket->InputBuffer,
                                    HeaderSize);
                if (Blob.CommandSize >
                    RequestPacket->InputBufferLength - HeaderSize)
                {
                    Status = ERROR_INSUFFICIENT_BUFFER;
                    break;
                }

                if (!VirtGpuCreateBlobResource(DeviceExtension,
                                               &Blob,
                                               Blob.CommandSize != 0 ?
                                                   ((PUCHAR)RequestPacket->InputBuffer +
                                                    HeaderSize) :
                                                   NULL,
                                               Blob.CommandSize))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                VideoPortMoveMemory(RequestPacket->OutputBuffer,
                                    &Blob,
                                    HeaderSize);
                RequestPacket->StatusBlock->Information = HeaderSize;
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_DESTROY_RESOURCE:
            if (!DeviceExtension->VirglSupported &&
                !DeviceExtension->ResourceBlobSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if (RequestPacket->InputBufferLength < sizeof(VIRTGPU_3D_RESOURCE))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_RESOURCE Resource = RequestPacket->InputBuffer;

                Status = VirtGpuDestroy3DResource(DeviceExtension,
                                                  Resource->ResourceId) ?
                         NO_ERROR : ERROR_INVALID_PARAMETER;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_ASSIGN_UUID:
            if (!DeviceExtension->ResourceUuidSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if ((RequestPacket->InputBufferLength < sizeof(VIRTGPU_3D_RESOURCE_UUID)) ||
                (RequestPacket->OutputBufferLength < sizeof(VIRTGPU_3D_RESOURCE_UUID)))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                VIRTGPU_3D_RESOURCE_UUID ResourceUuid;

                VideoPortMoveMemory(&ResourceUuid,
                                    RequestPacket->InputBuffer,
                                    sizeof(ResourceUuid));
                if (!VirtGpuAssignResourceUuid(DeviceExtension, &ResourceUuid))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                VideoPortMoveMemory(RequestPacket->OutputBuffer,
                                    &ResourceUuid,
                                    sizeof(ResourceUuid));
                RequestPacket->StatusBlock->Information = sizeof(ResourceUuid);
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_MAP_BLOB:
            if (!DeviceExtension->ResourceBlobSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if ((RequestPacket->InputBufferLength < sizeof(VIRTGPU_3D_MAP_BLOB)) ||
                (RequestPacket->OutputBufferLength < sizeof(VIRTGPU_3D_MAP_BLOB)))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                VIRTGPU_3D_MAP_BLOB Map;

                VideoPortMoveMemory(&Map,
                                    RequestPacket->InputBuffer,
                                    sizeof(Map));
                if (!VirtGpuMapBlobResource(DeviceExtension, &Map))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                VideoPortMoveMemory(RequestPacket->OutputBuffer,
                                    &Map,
                                    sizeof(Map));
                RequestPacket->StatusBlock->Information = sizeof(Map);
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_UNMAP_BLOB:
            if (!DeviceExtension->ResourceBlobSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if (RequestPacket->InputBufferLength < sizeof(VIRTGPU_3D_RESOURCE))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_RESOURCE Resource = RequestPacket->InputBuffer;

                Status = VirtGpuUnmapBlobResource(DeviceExtension,
                                                  Resource->ResourceId) ?
                         NO_ERROR : ERROR_INVALID_PARAMETER;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_ATTACH_RESOURCE:
        case IOCTL_VIDEO_VIRTGPU_3D_DETACH_RESOURCE:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if (RequestPacket->InputBufferLength <
                sizeof(VIRTGPU_3D_CONTEXT_RESOURCE))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_CONTEXT_RESOURCE ContextResource =
                    RequestPacket->InputBuffer;
                BOOLEAN Attach =
                    RequestPacket->IoControlCode ==
                    IOCTL_VIDEO_VIRTGPU_3D_ATTACH_RESOURCE;

                Status = VirtGpuContextResource(DeviceExtension,
                                                Attach,
                                                ContextResource->ContextId,
                                                ContextResource->ResourceId) ?
                         NO_ERROR : ERROR_INVALID_PARAMETER;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_TRANSFER_TO_HOST:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if (RequestPacket->InputBufferLength <
                offsetof(VIRTGPU_3D_TRANSFER, Data))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_TRANSFER Transfer = RequestPacket->InputBuffer;
                PVIRTGPU_3D_RESOURCE_STATE Resource;
                ULONG HeaderSize = offsetof(VIRTGPU_3D_TRANSFER, Data);
                ULONGLONG FenceId = 0;

                if (Transfer->Size > RequestPacket->InputBufferLength - HeaderSize)
                {
                    Status = ERROR_INSUFFICIENT_BUFFER;
                    break;
                }

                Resource = VirtGpuFindResource(DeviceExtension,
                                               Transfer->ResourceId);
                if ((Resource == NULL) ||
                    !Resource->BackingAttached ||
                    !VirtGpuTransferBackingRangeValid(Transfer,
                                                      Resource->BackingSize))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                VirtGpuCopyTransferToBacking(Resource, Transfer);

                Status = (VirtGpuTransferHost3DAsync(DeviceExtension,
                                                     Transfer,
                                                     &FenceId) ||
                          VirtGpuTransferHost3D(DeviceExtension,
                                                TRUE,
                                                Transfer,
                                                &FenceId)) ?
                         NO_ERROR : ERROR_INVALID_PARAMETER;
                if (Status == NO_ERROR)
                {
                    Transfer->FenceId = FenceId;
                    if ((RequestPacket->OutputBuffer != NULL) &&
                        (RequestPacket->OutputBufferLength >= HeaderSize))
                    {
                        VideoPortMoveMemory(RequestPacket->OutputBuffer,
                                            Transfer,
                                            HeaderSize);
                        RequestPacket->StatusBlock->Information = HeaderSize;
                    }
                }
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_TRANSFER_FROM_HOST:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if ((RequestPacket->InputBufferLength <
                 offsetof(VIRTGPU_3D_TRANSFER, Data)) ||
                (RequestPacket->OutputBufferLength <
                 offsetof(VIRTGPU_3D_TRANSFER, Data)))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_TRANSFER Input = RequestPacket->InputBuffer;
                PVIRTGPU_3D_TRANSFER Output = RequestPacket->OutputBuffer;
                PVIRTGPU_3D_RESOURCE_STATE Resource;
                ULONG HeaderSize = offsetof(VIRTGPU_3D_TRANSFER, Data);
                ULONG Information = HeaderSize;
                ULONGLONG FenceId = 0;

                if (Input->Size > RequestPacket->OutputBufferLength - HeaderSize)
                {
                    Status = ERROR_INSUFFICIENT_BUFFER;
                    break;
                }

                Resource = VirtGpuFindResource(DeviceExtension, Input->ResourceId);
                if ((Resource == NULL) ||
                    !Resource->BackingAttached ||
                    !VirtGpuTransferBackingRangeValid(Input,
                                                      Resource->BackingSize))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                if (!VirtGpuTransferHost3D(DeviceExtension, FALSE, Input, &FenceId))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                VideoPortMoveMemory(Output, Input, HeaderSize);
                Output->FenceId = FenceId;
                if (Input->Size != 0)
                    VirtGpuCopyBackingToTransfer(Resource, Output);
                Information += Input->Size;

                RequestPacket->StatusBlock->Information = Information;
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_SUBMIT:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if (RequestPacket->InputBufferLength < offsetof(VIRTGPU_3D_SUBMIT, Commands))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_SUBMIT Submit = RequestPacket->InputBuffer;
                ULONG HeaderSize = offsetof(VIRTGPU_3D_SUBMIT, Commands);
                ULONGLONG FenceId = 0;

                if (Submit->Size > RequestPacket->InputBufferLength - HeaderSize)
                {
                    Status = ERROR_INSUFFICIENT_BUFFER;
                    break;
                }

                Status = (VirtGpuSubmit3DAsync(DeviceExtension,
                                               Submit->ContextId,
                                               Submit->Commands,
                                               Submit->Size,
                                               Submit->RingIndex,
                                               &FenceId) ||
                          VirtGpuSubmit3D(DeviceExtension,
                                          Submit->ContextId,
                                          Submit->Commands,
                                          Submit->Size,
                                          Submit->RingIndex,
                                          &FenceId)) ?
                         NO_ERROR : ERROR_INVALID_PARAMETER;
                if (Status == NO_ERROR)
                {
                    Submit->FenceId = FenceId;
                    if ((RequestPacket->OutputBuffer != NULL) &&
                        (RequestPacket->OutputBufferLength >= HeaderSize))
                    {
                        VideoPortMoveMemory(RequestPacket->OutputBuffer,
                                            Submit,
                                            HeaderSize);
                        RequestPacket->StatusBlock->Information = HeaderSize;
                    }
                }
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_EXECUTE_BATCH:
            if (!DeviceExtension->VirglSupported)
            {
                Status = ERROR_INVALID_FUNCTION;
                break;
            }
            if ((RequestPacket->InputBufferLength <
                 offsetof(VIRTGPU_3D_BATCH, Commands)) ||
                (RequestPacket->OutputBufferLength <
                 offsetof(VIRTGPU_3D_BATCH, Commands)))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_BATCH Batch;
                ULONG HeaderSize = offsetof(VIRTGPU_3D_BATCH, Commands);
                ULONG BatchSize;

                Batch = RequestPacket->InputBuffer;
                if ((Batch->Size > VIRTGPU_3D_MAX_BATCH_BYTES) ||
                    (Batch->Size > RequestPacket->InputBufferLength - HeaderSize))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                BatchSize = HeaderSize + Batch->Size;
                if (RequestPacket->OutputBufferLength < BatchSize)
                {
                    Status = ERROR_INSUFFICIENT_BUFFER;
                    break;
                }

                VideoPortMoveMemory(RequestPacket->OutputBuffer,
                                    RequestPacket->InputBuffer,
                                    BatchSize);
                Batch = RequestPacket->OutputBuffer;

                if (!VirtGpuExecute3DBatch(DeviceExtension,
                                           Batch,
                                           BatchSize))
                {
                    Status = ERROR_INVALID_PARAMETER;
                    break;
                }

                RequestPacket->StatusBlock->Information = BatchSize;
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_3D_WAIT_FENCE:
            if (RequestPacket->OutputBufferLength < sizeof(VIRTGPU_3D_FENCE))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_3D_FENCE Fence = RequestPacket->OutputBuffer;

                if (RequestPacket->InputBufferLength >= sizeof(*Fence))
                {
                    VideoPortMoveMemory(Fence,
                                        RequestPacket->InputBuffer,
                                        sizeof(*Fence));
                }
                else
                {
                    VideoPortZeroMemory(Fence, sizeof(*Fence));
                }

                Fence->Completed =
                    (Fence->FenceId != 0) &&
                    (Fence->FenceId <= DeviceExtension->CompletedFenceId);
                Fence->Reserved = 0;
                RequestPacket->StatusBlock->Information = sizeof(*Fence);
                Status = NO_ERROR;
            }
            break;

        case IOCTL_VIDEO_VIRTGPU_QUERY_EDID:
            if (RequestPacket->OutputBufferLength < sizeof(VIRTGPU_EDID))
            {
                Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            {
                PVIRTGPU_EDID Edid = RequestPacket->OutputBuffer;

                VideoPortZeroMemory(Edid, sizeof(*Edid));
                Edid->Size = DeviceExtension->EdidSize;
                Edid->Valid = DeviceExtension->EdidValid ? 1 : 0;
                if (DeviceExtension->EdidSize != 0)
                {
                    VideoPortMoveMemory(Edid->Data,
                                        DeviceExtension->Edid,
                                        DeviceExtension->EdidSize);
                }

                RequestPacket->StatusBlock->Information = sizeof(*Edid);
                Status = NO_ERROR;
            }
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
VirtGpuResetHw(
    _In_ PVOID DeviceExtension,
    _In_ ULONG Columns,
    _In_ ULONG Rows)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(Columns);
    UNREFERENCED_PARAMETER(Rows);
    return FALSE;
}

VOID
NTAPI
VirtGpuTimer(_In_ PVOID HwDeviceExtension)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;

    if (!DeviceExtension->HardwareReady || !DeviceExtension->FlushPending)
    {
        VideoPortStopTimer(DeviceExtension);
        return;
    }

    if (VirtGpuFlush(DeviceExtension))
        VideoPortStopTimer(DeviceExtension);
}

VP_STATUS
NTAPI
VirtGpuGetPowerState(
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
VirtGpuSetPowerState(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG HwId,
    _In_ PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    UNREFERENCED_PARAMETER(HwDeviceExtension);
    UNREFERENCED_PARAMETER(HwId);

    if (VideoPowerControl == NULL)
        return ERROR_INVALID_PARAMETER;

    return (VideoPowerControl->PowerState == VideoPowerOn) ?
           NO_ERROR :
           ERROR_INVALID_FUNCTION;
}

VP_STATUS
NTAPI
VirtGpuGetVideoChildDescriptor(
    _In_ PVOID HwDeviceExtension,
    _In_ PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
    _Out_ PVIDEO_CHILD_TYPE VideoChildType,
    _Out_ PUCHAR ChildDescriptor,
    _Out_ PULONG UId,
    _Out_ PULONG Unused)
{
    PVIRTGPU_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    ULONG CopySize;

    if ((ChildEnumInfo == NULL) ||
        (VideoChildType == NULL) ||
        (UId == NULL) ||
        (Unused == NULL) ||
        (ChildEnumInfo->Size != sizeof(VIDEO_CHILD_ENUM_INFO)))
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
        if ((ChildDescriptor != NULL) &&
            (ChildEnumInfo->ChildDescriptorSize >= 128) &&
            DeviceExtension->EdidValid)
        {
            CopySize = DeviceExtension->EdidSize;
            if (CopySize > ChildEnumInfo->ChildDescriptorSize)
                CopySize = ChildEnumInfo->ChildDescriptorSize;
            if (CopySize > VIRTGPU_SHARED_EDID_SIZE)
                CopySize = VIRTGPU_SHARED_EDID_SIZE;

            VideoPortMoveMemory(ChildDescriptor,
                                DeviceExtension->Edid,
                                CopySize);
        }
        return VIDEO_ENUM_MORE_DEVICES;
    }

    return VIDEO_ENUM_NO_MORE_DEVICES;
}
