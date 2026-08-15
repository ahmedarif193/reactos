/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SDIO transport: CMD52/CMD53, backplane window, RAM write
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"
#include <devpkey.h>
#include <initguid.h>
#include <reactos/drivers/cyw43455sdio.h>

#define NDEBUG
#include <debug.h>

#define CYW_SDIO_MAX_FUNCTION          7
#define CYW_SDIO_MAX_ADDRESS           0x1FFFFUL
#define CYW_SDIO_MAX_COUNT             512
#define CYW_SDIO_MAX_BLOCK_COUNT       511
#define CYW_SDIO_MAX_BLOCK_SIZE        0x0FFF

typedef struct _CYW_SDIO_CARD_IDENTITY
{
    UNICODE_STRING Prefix;
    UNICODE_STRING Suffix;
} CYW_SDIO_CARD_IDENTITY, *PCYW_SDIO_CARD_IDENTITY;

static const DEVPROPKEY CywDevpkeyDeviceInstanceId =
{
    {0x78c34fc8, 0x104a, 0x4aca,
     {0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57}},
    256
};

static
BOOLEAN
CywSdioParseCardIdentity(
    _In_z_ PCWSTR DevicePath,
    _In_ WCHAR ComponentSeparator,
    _Out_ PCYW_SDIO_CARD_IDENTITY Identity)
{
    static const WCHAR FunctionMarker[] = L"_FUNC_";
    PCWSTR Component;
    PCWSTR Cursor;
    PCWSTR Function;
    PCWSTR FunctionNumber;
    PCWSTR FunctionEnd;
    PCWSTR ComponentEnd;
    SIZE_T PrefixLength;
    SIZE_T SuffixLength;

    Function = wcsstr(DevicePath, FunctionMarker);
    if (Function == NULL)
        return FALSE;

    Component = DevicePath;
    for (Cursor = DevicePath; Cursor < Function; Cursor++)
    {
        if (*Cursor == ComponentSeparator)
            Component = Cursor + 1;
    }

    FunctionNumber = Function + RTL_NUMBER_OF(FunctionMarker) - 1;
    FunctionEnd = FunctionNumber;
    if (*FunctionNumber < L'0' || *FunctionNumber > L'9')
        return FALSE;
    do
    {
        FunctionEnd++;
    } while (*FunctionEnd >= L'0' && *FunctionEnd <= L'9');

    ComponentEnd = FunctionEnd;
    while (*ComponentEnd != UNICODE_NULL &&
           *ComponentEnd != ComponentSeparator)
    {
        ComponentEnd++;
    }

    PrefixLength = (SIZE_T)(FunctionNumber - Component);
    SuffixLength = (SIZE_T)(ComponentEnd - FunctionEnd);
    if (PrefixLength > MAXUSHORT / sizeof(WCHAR) ||
        SuffixLength > MAXUSHORT / sizeof(WCHAR))
    {
        return FALSE;
    }

    Identity->Prefix.Buffer = (PWSTR)Component;
    Identity->Prefix.Length = (USHORT)(PrefixLength * sizeof(WCHAR));
    Identity->Prefix.MaximumLength = Identity->Prefix.Length;
    Identity->Suffix.Buffer = (PWSTR)FunctionEnd;
    Identity->Suffix.Length = (USHORT)(SuffixLength * sizeof(WCHAR));
    Identity->Suffix.MaximumLength = Identity->Suffix.Length;
    return TRUE;
}

static
BOOLEAN
CywSdioInterfaceMatchesCard(
    _In_z_ PCWSTR AdapterInstanceId,
    _In_z_ PCWSTR InterfaceName)
{
    CYW_SDIO_CARD_IDENTITY AdapterIdentity;
    CYW_SDIO_CARD_IDENTITY InterfaceIdentity;

    if (!CywSdioParseCardIdentity(AdapterInstanceId,
                                  L'\\',
                                  &AdapterIdentity) ||
        !CywSdioParseCardIdentity(InterfaceName,
                                  L'#',
                                  &InterfaceIdentity))
    {
        return FALSE;
    }

    return RtlEqualUnicodeString(&AdapterIdentity.Prefix,
                                 &InterfaceIdentity.Prefix,
                                 TRUE) &&
           RtlEqualUnicodeString(&AdapterIdentity.Suffix,
                                 &InterfaceIdentity.Suffix,
                                 TRUE);
}

static
NTSTATUS
CywSdioGetInstanceId(
    _In_ PDEVICE_OBJECT PhysicalDevice,
    _Outptr_ PWSTR *InstanceId)
{
    DEVPROPTYPE PropertyType;
    ULONG RequiredLength = 0;
    PWSTR Buffer;
    NTSTATUS Status;

    *InstanceId = NULL;
    Status = IoGetDevicePropertyData(PhysicalDevice,
                                     &CywDevpkeyDeviceInstanceId,
                                     0,
                                     0,
                                     0,
                                     NULL,
                                     &RequiredLength,
                                     &PropertyType);
    if (Status != STATUS_BUFFER_TOO_SMALL &&
        Status != STATUS_BUFFER_OVERFLOW)
    {
        return Status;
    }
    if (PropertyType != DEVPROP_TYPE_STRING ||
        RequiredLength < sizeof(WCHAR))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    Buffer = CywAllocate(RequiredLength);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoGetDevicePropertyData(PhysicalDevice,
                                     &CywDevpkeyDeviceInstanceId,
                                     0,
                                     0,
                                     RequiredLength,
                                     Buffer,
                                     &RequiredLength,
                                     &PropertyType);
    if (!NT_SUCCESS(Status) || PropertyType != DEVPROP_TYPE_STRING ||
        RequiredLength < sizeof(WCHAR) ||
        Buffer[RequiredLength / sizeof(WCHAR) - 1] != UNICODE_NULL)
    {
        CywFree(Buffer);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    }

    *InstanceId = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywSdioOpenControl(
    _Inout_ PCYW_ADAPTER Adapter)
{
    PWSTR InterfaceList;
    PWSTR Cursor;
    PWSTR AdapterInstanceId;
    NTSTATUS Status;

    Status = CywSdioGetInstanceId(Adapter->Pdo, &AdapterInstanceId);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IoGetDeviceInterfaces(
        &GUID_DEVINTERFACE_REACTOS_CYW43455_SDIO_CONTROL,
        NULL,
        0,
        &InterfaceList);
    if (!NT_SUCCESS(Status))
    {
        CywFree(AdapterInstanceId);
        return Status;
    }

    Status = STATUS_DEVICE_NOT_CONNECTED;
    for (Cursor = InterfaceList; *Cursor != UNICODE_NULL;
         Cursor += wcslen(Cursor) + 1)
    {
        UNICODE_STRING InterfaceName;
        PFILE_OBJECT FileObject;
        PDEVICE_OBJECT DeviceObject;

        if (!CywSdioInterfaceMatchesCard(AdapterInstanceId, Cursor))
            continue;

        RtlInitUnicodeString(&InterfaceName, Cursor);
        Status = IoGetDeviceObjectPointer(&InterfaceName,
                                          FILE_READ_DATA |
                                              FILE_WRITE_DATA |
                                              SYNCHRONIZE,
                                          &FileObject,
                                          &DeviceObject);
        if (NT_SUCCESS(Status))
        {
            Adapter->SdioControlFileObject = FileObject;
            Adapter->SdioControlDeviceObject = DeviceObject;
            break;
        }
    }

    ExFreePool(InterfaceList);
    CywFree(AdapterInstanceId);
    return Status;
}

static
VOID
CywSdioCloseControl(
    _Inout_ PCYW_ADAPTER Adapter)
{
    if (Adapter->SdioControlFileObject != NULL)
        ObDereferenceObject(Adapter->SdioControlFileObject);
    Adapter->SdioControlFileObject = NULL;
    Adapter->SdioControlDeviceObject = NULL;
}

static
NTSTATUS
CywSdioControlTransfer(
    _In_ PCYW_ADAPTER Adapter,
    _In_ BOOLEAN Direct,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN BlockMode,
    _In_ BOOLEAN Increment,
    _In_ ULONG Address,
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONG BlockSize)
{
    PCYW43455_SDIO_TRANSFER Transfer;
    ULONG TransferSize;
    ULONG InputLength;
    ULONG OutputLength;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    if (Adapter->SdioControlDeviceObject == NULL ||
        Adapter->SdioControlFileObject == NULL || Buffer == NULL ||
        Length == 0 || Length > CYW43455_SDIO_MAX_TRANSFER ||
        Length > MAXULONG - CYW43455_SDIO_TRANSFER_HEADER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    TransferSize = CYW43455_SDIO_TRANSFER_SIZE(Length);
    Transfer = CywAllocate(TransferSize);
    if (Transfer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Transfer->Version = CYW43455_SDIO_TRANSPORT_VERSION;
    Transfer->Size = CYW43455_SDIO_TRANSFER_HEADER_SIZE;
    Transfer->Address = Address;
    Transfer->Length = Length;
    Transfer->BlockSize = BlockSize;
    if (Direct)
        Transfer->Flags |= CYW43455_SDIO_TRANSFER_DIRECT;
    if (Write)
    {
        Transfer->Flags |= CYW43455_SDIO_TRANSFER_WRITE;
        RtlCopyMemory(Transfer->Data, Buffer, Length);
    }
    if (BlockMode)
        Transfer->Flags |= CYW43455_SDIO_TRANSFER_BLOCK_MODE;
    if (Increment)
        Transfer->Flags |= CYW43455_SDIO_TRANSFER_INCREMENT;

    InputLength = Write ? TransferSize : CYW43455_SDIO_TRANSFER_HEADER_SIZE;
    OutputLength = Write ? 0 : TransferSize;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_CYW43455_SDIO_TRANSFER,
                                        Adapter->SdioControlDeviceObject,
                                        Transfer,
                                        InputLength,
                                        Write ? NULL : Transfer,
                                        OutputLength,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
    {
        CywFree(Transfer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(Adapter->SdioControlDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatus.Status;
    }

    if (NT_SUCCESS(Status) && !Write)
    {
        if (IoStatus.Information < TransferSize)
            Status = STATUS_DEVICE_DATA_ERROR;
        else
            RtlCopyMemory(Buffer, Transfer->Data, Length);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: FN1 %s%s address 0x%05lx length %lu failed 0x%08lx\n",
                Write ? "write" : "read", Direct ? "-direct" : "",
                Address, Length, Status);
    }

    CywFree(Transfer);
    return Status;
}

PVOID
CywAllocate(
    _In_ ULONG Size)
{
    return ExAllocatePoolZero(NonPagedPool, Size, CYW_TAG);
}

VOID
CywFree(
    _In_ PVOID Buffer)
{
    if (Buffer != NULL)
    {
        ExFreePoolWithTag(Buffer, CYW_TAG);
    }
}

NTSTATUS
CywSdioOpen(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;

    if (Adapter == NULL || Adapter->Pdo == NULL || Adapter->SdBusOpened)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = SdBusOpenInterface(Adapter->Pdo,
                                &Adapter->SdBus,
                                sizeof(SDBUS_INTERFACE_STANDARD),
                                SDBUS_INTERFACE_VERSION);
    if (NT_SUCCESS(Status))
    {
        Adapter->SdBusOpened = TRUE;
        Status = CywSdioOpenControl(Adapter);
        if (!NT_SUCCESS(Status))
        {
            Adapter->SdBus.InterfaceDereference(Adapter->SdBus.Context);
            Adapter->SdBusOpened = FALSE;
            RtlZeroMemory(&Adapter->SdBus, sizeof(Adapter->SdBus));
        }
    }

    return Status;
}

VOID
CywSdioClose(
    _In_ PCYW_ADAPTER Adapter)
{
    CywSdioCloseControl(Adapter);
    if (Adapter->SdBusOpened && Adapter->SdBus.InterfaceDereference != NULL)
    {
        Adapter->SdBus.InterfaceDereference(Adapter->SdBus.Context);
    }
    Adapter->SdBusOpened = FALSE;
    RtlZeroMemory(&Adapter->SdBus, sizeof(Adapter->SdBus));
}

NTSTATUS
CywSdioReadByte(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Value)
{
    SDBUS_REQUEST_PACKET Packet;
    NTSTATUS Status;

    if (Adapter == NULL || !Adapter->SdBusOpened ||
        Adapter->SdBus.Context == NULL || Value == NULL ||
        Function > CYW_SDIO_MAX_FUNCTION || Address > CYW_SDIO_MAX_ADDRESS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Function == CYW_SDIO_FUNC_BACKPLANE)
    {
        return CywSdioControlTransfer(Adapter,
                                      TRUE,
                                      FALSE,
                                      FALSE,
                                      FALSE,
                                      Address,
                                      Value,
                                      sizeof(*Value),
                                      0);
    }

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_DIRECT);
    Packet.Parameters.IoDirect.Function = Function;
    Packet.Parameters.IoDirect.Write = FALSE;
    Packet.Parameters.IoDirect.RawMode = FALSE;
    Packet.Parameters.IoDirect.Address = Address;

    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
    if (NT_SUCCESS(Status))
    {
        *Value = Packet.Parameters.IoDirect.DataOut;
    }

    return Status;
}

NTSTATUS
CywSdioWriteByte(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ UCHAR Value)
{
    SDBUS_REQUEST_PACKET Packet;

    if (Adapter == NULL || !Adapter->SdBusOpened ||
        Adapter->SdBus.Context == NULL ||
        Function > CYW_SDIO_MAX_FUNCTION || Address > CYW_SDIO_MAX_ADDRESS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Function == CYW_SDIO_FUNC_BACKPLANE)
    {
        return CywSdioControlTransfer(Adapter,
                                      TRUE,
                                      TRUE,
                                      FALSE,
                                      FALSE,
                                      Address,
                                      &Value,
                                      sizeof(Value),
                                      0);
    }

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_DIRECT);
    Packet.Parameters.IoDirect.Function = Function;
    Packet.Parameters.IoDirect.Write = TRUE;
    Packet.Parameters.IoDirect.RawMode = FALSE;
    Packet.Parameters.IoDirect.Address = Address;
    Packet.Parameters.IoDirect.DataIn = Value;

    return SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
}

NTSTATUS
CywRegisterDmaBuf(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Buffer,
    _In_ ULONG Size)
{
    PCYW_DMA_BUF Buf;
    SIZE_T MdlSize;
    ULONG PageCount;

    if (Adapter == NULL || Buffer == NULL || Size == 0 ||
        Adapter->DmaBufCount >= CYW_DMA_BUF_COUNT ||
        (ULONG_PTR)Buffer > MAXULONG_PTR - (Size - 1))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Buf = &Adapter->DmaBufs[Adapter->DmaBufCount];

    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Buffer, Size);
    MdlSize = sizeof(MDL) + sizeof(PFN_NUMBER) * (SIZE_T)PageCount;
    if (MdlSize > MAXULONG)
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    Buf->Mdl = CywAllocate((ULONG)MdlSize);
    if (Buf->Mdl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Buf->Buffer = Buffer;
    Buf->Size = Size;
    Adapter->DmaBufCount++;
    return STATUS_SUCCESS;
}

VOID
CywFreeDmaBufs(
    _In_ PCYW_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < Adapter->DmaBufCount; i++)
    {
        CywFree(Adapter->DmaBufs[i].Mdl);
        Adapter->DmaBufs[i].Mdl = NULL;
        Adapter->DmaBufs[i].Buffer = NULL;
        Adapter->DmaBufs[i].Size = 0;
    }
    Adapter->DmaBufCount = 0;
}

/* Transfers overwhelmingly target one of the persistent adapter buffers, each
 * of which is serialized by its owning lock. Those reuse a preallocated MDL,
 * re-pointed at the requested range, instead of allocating one per command. */
static
PMDL
CywAcquireMdl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _Out_ PBOOLEAN Owned)
{
    PMDL Mdl;
    ULONG i;
    ULONG_PTR BufferAddress;

    if (Owned == NULL)
    {
        return NULL;
    }
    *Owned = FALSE;

    if (Adapter == NULL || Buffer == NULL || Length == 0 ||
        (ULONG_PTR)Buffer > MAXULONG_PTR - (Length - 1))
    {
        return NULL;
    }

    BufferAddress = (ULONG_PTR)Buffer;
    for (i = 0; i < Adapter->DmaBufCount; i++)
    {
        PCYW_DMA_BUF Buf = &Adapter->DmaBufs[i];
        ULONG_PTR RegisteredAddress = (ULONG_PTR)Buf->Buffer;
        SIZE_T Offset;

        if (BufferAddress >= RegisteredAddress)
        {
            Offset = BufferAddress - RegisteredAddress;
            if (Offset <= Buf->Size && Length <= Buf->Size - Offset)
            {
                Mdl = Buf->Mdl;
                MmInitializeMdl(Mdl, Buffer, Length);
                MmBuildMdlForNonPagedPool(Mdl);
                return Mdl;
            }
        }
    }

    Mdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, NULL);
    if (Mdl != NULL)
    {
        MmBuildMdlForNonPagedPool(Mdl);
    }
    *Owned = TRUE;
    return Mdl;
}

static
NTSTATUS
CywSdioRw(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN BlockMode,
    _In_ ULONG BlockSize)
{
    SDBUS_REQUEST_PACKET Packet;
    PMDL Mdl;
    BOOLEAN OwnedMdl;
    NTSTATUS Status;
    ULONG Count;
    BOOLEAN Increment;

    if (Adapter == NULL || !Adapter->SdBusOpened ||
        Adapter->SdBus.Context == NULL || Buffer == NULL || Length == 0 ||
        Function > CYW_SDIO_MAX_FUNCTION || Address > CYW_SDIO_MAX_ADDRESS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (BlockMode)
    {
        if (BlockSize == 0 || BlockSize > CYW_SDIO_MAX_BLOCK_SIZE ||
            (Length % BlockSize) != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Count = Length / BlockSize;
    }
    else
    {
        if (BlockSize != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Count = Length;
    }

    if (Count == 0 ||
        Count > (BlockMode ? CYW_SDIO_MAX_BLOCK_COUNT :
                             CYW_SDIO_MAX_COUNT))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Function 2 is a receive FIFO; reads must keep the CMD53 address fixed. */
    Increment = !(Function == CYW_SDIO_FUNC_RADIO && !Write);
    if (Increment && Length - 1 > CYW_SDIO_MAX_ADDRESS - Address)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Function == CYW_SDIO_FUNC_BACKPLANE)
    {
        return CywSdioControlTransfer(Adapter,
                                      FALSE,
                                      Write,
                                      BlockMode,
                                      Increment,
                                      Address,
                                      Buffer,
                                      Length,
                                      BlockSize);
    }

    Mdl = CywAcquireMdl(Adapter, Buffer, Length, &OwnedMdl);
    if (Mdl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_EXTENDED);
    Packet.Parameters.IoExtended.Function = Function;
    Packet.Parameters.IoExtended.Write = Write;
    Packet.Parameters.IoExtended.BlockMode = BlockMode;
    Packet.Parameters.IoExtended.Increment = Increment;
    Packet.Parameters.IoExtended.Address = Address;
    Packet.Parameters.IoExtended.BlockCount = Count;
    Packet.Parameters.IoExtended.BlockSize = BlockSize;
    Packet.Parameters.IoExtended.Mdl = Mdl;

    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);

    if (OwnedMdl)
    {
        IoFreeMdl(Mdl);
    }
    return Status;
}

NTSTATUS
CywSdioReadBytes(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    return CywSdioRw(Adapter, Function, FALSE, Address, Buffer, Length, FALSE, 0);
}

NTSTATUS
CywSdioReadBlocks(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONG BlockSize)
{
    return CywSdioRw(Adapter, Function, FALSE, Address, Buffer, Length, TRUE, BlockSize);
}

NTSTATUS
CywSdioWriteBytes(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    return CywSdioRw(Adapter, Function, TRUE, Address, Buffer, Length, FALSE, 0);
}

NTSTATUS
CywSdioWriteBlocks(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONG BlockSize)
{
    return CywSdioRw(Adapter, Function, TRUE, Address, Buffer, Length, TRUE, BlockSize);
}

NTSTATUS
CywSdioEnableFunction(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function)
{
    NTSTATUS Status;
    UCHAR Enable;
    UCHAR Ready;
    ULONG Retry;
    LARGE_INTEGER Delay;

    if (Adapter == NULL || Function == 0 ||
        Function > CYW_SDIO_MAX_FUNCTION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_IOEx, &Enable);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Enable |= (1u << Function);
    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_IOEx, Enable);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Retry = 0; Retry < 500; Retry++)
    {
        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_IORx, &Ready);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (Ready & (1u << Function))
        {
            return STATUS_SUCCESS;
        }
        Delay.QuadPart = -10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    Enable &= (UCHAR)~(1u << Function);
    (VOID)CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                           SDIO_CCCR_IOEx, Enable);
    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
CywSdioSetBlockSize(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG BlockSize)
{
    ULONG Fbr = (ULONG)Function * 0x100;
    NTSTATUS Status;
    UCHAR OldLow;
    UCHAR OldHigh;
    UCHAR VerifyLow;
    UCHAR VerifyHigh;

    if (Adapter == NULL || Function > CYW_SDIO_MAX_FUNCTION ||
        BlockSize == 0 || BlockSize > CYW_SDIO_MAX_BLOCK_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                             Fbr + 0x10, &OldLow);
    if (NT_SUCCESS(Status))
    {
        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                                 Fbr + 0x11, &OldHigh);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, Fbr + 0x10, (UCHAR)(BlockSize & 0xFF));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, Fbr + 0x11, (UCHAR)((BlockSize >> 8) & 0xFF));
    if (!NT_SUCCESS(Status))
    {
        (VOID)CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                               Fbr + 0x10, OldLow);
        return Status;
    }

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                             Fbr + 0x10, &VerifyLow);
    if (NT_SUCCESS(Status))
    {
        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                                 Fbr + 0x11, &VerifyHigh);
    }
    if (!NT_SUCCESS(Status) || VerifyLow != (UCHAR)BlockSize ||
        VerifyHigh != (UCHAR)(BlockSize >> 8))
    {
        (VOID)CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                               Fbr + 0x10, OldLow);
        (VOID)CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                               Fbr + 0x11, OldHigh);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
CywBackplaneSetWindowLocked(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    ULONG Window = Address & SBSDIO_SBWINDOW_MASK;
    NTSTATUS Status;

    if (Window == Adapter->CurrentBackplaneWindow)
    {
        return STATUS_SUCCESS;
    }

    /* A partial programming failure makes the old cached value untrustworthy. */
    Adapter->CurrentBackplaneWindow = MAXULONG;

    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                              SBSDIO_FUNC1_SBADDRLOW, (UCHAR)((Window >> 8) & 0x80));
    if (NT_SUCCESS(Status))
    {
        Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                  SBSDIO_FUNC1_SBADDRMID, (UCHAR)((Window >> 16) & 0xFF));
    }
    if (NT_SUCCESS(Status))
    {
        Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                  SBSDIO_FUNC1_SBADDRHIGH, (UCHAR)((Window >> 24) & 0xFF));
    }

    if (NT_SUCCESS(Status))
    {
        Adapter->CurrentBackplaneWindow = Window;
    }
    return Status;
}

NTSTATUS
CywBackplaneSetWindow(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    NTSTATUS Status;

    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive,
                          KernelMode, FALSE, NULL);
    Status = CywBackplaneSetWindowLocked(Adapter, Address);
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return Status;
}

NTSTATUS
CywBackplaneReadl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PULONG Value)
{
    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return CywBackplaneReadlSc(Adapter, Address, Value,
                               Adapter->ControlBuffer);
}

NTSTATUS
CywBackplaneWritel(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return CywBackplaneWritelSc(Adapter, Address, Value,
                                Adapter->ControlBuffer);
}

NTSTATUS
CywBackplaneReadlSc(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PULONG Value,
    _Inout_ PUCHAR Scratch)
{
    NTSTATUS Status;
    ULONG Offset;

    if (Adapter == NULL || Value == NULL || Scratch == NULL ||
        (Address & SBSDIO_SB_OFT_ADDR_MASK) >
            SBSDIO_SB_OFT_ADDR_LIMIT - sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive,
                          KernelMode, FALSE, NULL);
    Status = CywBackplaneSetWindowLocked(Adapter, Address);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
        return Status;
    }

    Offset = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;
    Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE, Offset, Scratch, 4);
    if (NT_SUCCESS(Status) && Value != NULL)
    {
        *Value = ((PULONG)Scratch)[0];
    }
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return Status;
}

NTSTATUS
CywBackplaneWritelSc(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value,
    _Inout_ PUCHAR Scratch)
{
    NTSTATUS Status;
    ULONG Offset;

    if (Adapter == NULL || Scratch == NULL ||
        (Address & SBSDIO_SB_OFT_ADDR_MASK) >
            SBSDIO_SB_OFT_ADDR_LIMIT - sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive,
                          KernelMode, FALSE, NULL);
    Status = CywBackplaneSetWindowLocked(Adapter, Address);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
        return Status;
    }

    ((PULONG)Scratch)[0] = Value;
    Offset = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;
    Status = CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                               Offset, Scratch, 4);
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return Status;
}

NTSTATUS
CywRamWrite(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    NTSTATUS Status;
    ULONG WindowOffset;
    ULONG Chunk;
    ULONG Transfer;

    if (Adapter == NULL || (Buffer == NULL && Length != 0) ||
        (Length != 0 && Address > MAXULONG - (Length - 1)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive,
                          KernelMode, FALSE, NULL);
    while (Length > 0)
    {
        Status = CywBackplaneSetWindowLocked(Adapter, Address);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
            return Status;
        }

        WindowOffset = Address & SBSDIO_SB_OFT_ADDR_MASK;
        Chunk = SBSDIO_SB_OFT_ADDR_LIMIT - WindowOffset;
        if (Chunk > Length)
        {
            Chunk = Length;
        }

        Transfer = Chunk;
        if (Transfer >= CYW_F1_BLOCKSIZE)
        {
            ULONG Blocks = Transfer / CYW_F1_BLOCKSIZE;
            /* Keep the block count nonzero in the CMD53 argument. The
             * CYW43455 enters R5 error state after a 512-block command. */
            if (Blocks > CYW_SDIO_MAX_BLOCK_COUNT)
            {
                Blocks = CYW_SDIO_MAX_BLOCK_COUNT;
            }
            Transfer = Blocks * CYW_F1_BLOCKSIZE;
            Status = CywSdioRw(Adapter, CYW_SDIO_FUNC_BACKPLANE, TRUE,
                               WindowOffset | SBSDIO_SB_ACCESS_2_4B_FLAG,
                               Buffer, Transfer, TRUE, CYW_F1_BLOCKSIZE);
        }
        else
        {
            Status = CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                       WindowOffset | SBSDIO_SB_ACCESS_2_4B_FLAG,
                                       Buffer, Transfer);
        }
        if (!NT_SUCCESS(Status))
        {
            KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
            return Status;
        }

        Address += Transfer;
        Buffer += Transfer;
        Length -= Transfer;
    }

    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return STATUS_SUCCESS;
}
