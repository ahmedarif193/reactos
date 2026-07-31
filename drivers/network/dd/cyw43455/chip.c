/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Chip recognition, firmware download and bring-up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
CywReadFile(
    _In_ PCWSTR Path,
    _Outptr_ PUCHAR *Buffer,
    _Out_ PULONG Size)
{
    UNICODE_STRING FileName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    FILE_STANDARD_INFORMATION FileInfo;
    HANDLE Handle;
    NTSTATUS Status;
    PUCHAR Data;
    ULONG Length;
    ULONG AllocationSize;

    if (Path == NULL || Buffer == NULL || Size == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Buffer = NULL;
    *Size = 0;

    RtlInitUnicodeString(&FileName, Path);
    InitializeObjectAttributes(&ObjectAttributes, &FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    Status = ZwCreateFile(&Handle, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes,
                          &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
                          FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ZwQueryInformationFile(Handle, &IoStatus, &FileInfo,
                                    sizeof(FileInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status) || FileInfo.EndOfFile.HighPart != 0 ||
        FileInfo.EndOfFile.LowPart == 0)
    {
        ZwClose(Handle);
        return NT_SUCCESS(Status) ? STATUS_FILE_INVALID : Status;
    }

    Length = FileInfo.EndOfFile.LowPart;
    if (Length > MAXULONG - (sizeof(ULONG) - 1))
    {
        ZwClose(Handle);
        return STATUS_FILE_TOO_LARGE;
    }
    AllocationSize = ALIGN_UP(Length, ULONG);
    Data = CywAllocate(AllocationSize);
    if (Data == NULL)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Data, Length, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status) || IoStatus.Information != Length)
    {
        CywFree(Data);
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    *Buffer = Data;
    *Size = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywClockRequest(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Request,
    _In_ UCHAR AvailMask,
    _In_ ULONG TimeoutMilliseconds)
{
    NTSTATUS Status;
    UCHAR Csr;
    ULONG Retry;
    LARGE_INTEGER Delay;

    if (Adapter == NULL ||
        (Request & ~(SBSDIO_CSR_MASK | SBSDIO_FORCE_HW_CLKREQ_OFF)) != 0 ||
        (AvailMask & ~SBSDIO_AVAIL_MASK) != 0 ||
        TimeoutMilliseconds == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                              SBSDIO_FUNC1_CHIPCLKCSR, Request);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Retry = 0; Retry < TimeoutMilliseconds; Retry++)
    {
        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                 SBSDIO_FUNC1_CHIPCLKCSR, &Csr);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if ((Csr & AvailMask) == AvailMask)
        {
            return STATUS_SUCCESS;
        }
        Delay.QuadPart = -10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
CywChipRecognize(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG RegData;
    UCHAR Csr;

    Status = CywClockRequest(Adapter,
                             SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ,
                             SBSDIO_ALP_AVAIL,
                             1000);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                             SBSDIO_FUNC1_CHIPCLKCSR, &Csr);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if ((Csr & ~SBSDIO_AVAIL_MASK) !=
        (SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                              SBSDIO_FUNC1_CHIPCLKCSR,
                              SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    KeStallExecutionProcessor(65);

    Status = CywBackplaneReadl(Adapter, SI_ENUM_BASE_DEFAULT, &RegData);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Adapter->ChipId = RegData & CID_ID_MASK;
    Adapter->ChipRev = (RegData & CID_REV_MASK) >> CID_REV_SHIFT;

    if (Adapter->ChipId != BRCM_CC_4345_CHIP_ID)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Adapter->RamBase = CYW43455_RAMBASE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywDownloadNvram(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG FirmwareSize)
{
    NTSTATUS Status;
    PUCHAR Raw;
    PUCHAR Stripped;
    ULONG RawSize;
    ULONG AllocationSize;
    ULONG OutLen;
    ULONG Words;
    ULONG Token;
    ULONG RamEnd;
    ULONG NvramAddress;
    ULONG NvramSize;
    ULONG i;

    if (Adapter == NULL || Adapter->ControlBuffer == NULL ||
        Adapter->RamSize == 0 || FirmwareSize == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywReadFile(CYW_FW_DIR CYW_FW_NVRAM, &Raw, &RawSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (RawSize > MAXULONG - 8)
    {
        CywFree(Raw);
        return STATUS_INTEGER_OVERFLOW;
    }
    AllocationSize = RawSize + 8;
    Stripped = CywAllocate(AllocationSize);
    if (Stripped == NULL)
    {
        CywFree(Raw);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    OutLen = 0;
    i = 0;
    while (i < RawSize)
    {
        if (Raw[i] == '#')
        {
            while (i < RawSize && Raw[i] != '\n')
            {
                i++;
            }
            continue;
        }
        if (Raw[i] == '\r')
        {
            i++;
            continue;
        }
        if (Raw[i] == '\n')
        {
            if (OutLen > 0 && Stripped[OutLen - 1] != '\0')
            {
                Stripped[OutLen++] = '\0';
            }
            i++;
            continue;
        }
        Stripped[OutLen++] = Raw[i++];
    }
    if (OutLen == 0)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Cleanup;
    }
    if (Stripped[OutLen - 1] != '\0')
    {
        Stripped[OutLen++] = '\0';
    }

    /* The firmware variable table is terminated by an additional NUL. */
    Stripped[OutLen++] = '\0';

    while ((OutLen % 4) != 0)
    {
        Stripped[OutLen++] = '\0';
    }

    Words = OutLen / 4;
    if (Words > MAXUSHORT || OutLen > MAXULONG - sizeof(Token))
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto Cleanup;
    }
    Token = CYW_NVRAM_TOKEN(Words);

    NvramSize = OutLen + sizeof(Token);
    if (Adapter->RamBase > MAXULONG - Adapter->RamSize ||
        Adapter->RamSize < NvramSize)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }
    RamEnd = Adapter->RamBase + Adapter->RamSize;
    NvramAddress = RamEnd - NvramSize;
    if (FirmwareSize > NvramAddress - Adapter->RamBase)
    {
        Status = STATUS_BUFFER_OVERFLOW;
        goto Cleanup;
    }

    Status = CywRamWrite(Adapter, NvramAddress, Stripped, OutLen);
    if (NT_SUCCESS(Status))
    {
        ((PULONG)Adapter->ControlBuffer)[0] = Token;
        Status = CywRamWrite(Adapter, NvramAddress + OutLen,
                             Adapter->ControlBuffer, sizeof(Token));
    }

Cleanup:
    CywFree(Stripped);
    CywFree(Raw);
    return Status;
}

static
NTSTATUS
CywDownloadClm(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;
    PUCHAR Blob;
    ULONG BlobSize;
    PCYW_DLOAD_DATA Chunk;
    ULONG HdrSize = FIELD_OFFSET(CYW_DLOAD_DATA, Data);
    ULONG Offset;

    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywReadFile(CYW_FW_DIR CYW_FW_CLM, &Blob, &BlobSize);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
            Status == STATUS_OBJECT_PATH_NOT_FOUND ||
            Status == STATUS_NO_SUCH_FILE)
        {
            return STATUS_SUCCESS;
        }
        return Status;
    }

    Chunk = CywAllocate(HdrSize + CYW_CLM_CHUNK_LEN);
    if (Chunk == NULL)
    {
        CywFree(Blob);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Offset = 0;
    while (Offset < BlobSize)
    {
        ULONG Len = BlobSize - Offset;
        ULONG Flag = CYW_DLOAD_HANDLER_VER << CYW_DLOAD_FLAG_VER_SHIFT;

        if (Len > CYW_CLM_CHUNK_LEN)
        {
            Len = CYW_CLM_CHUNK_LEN;
        }
        if (Offset == 0)
        {
            Flag |= CYW_DL_BEGIN;
        }
        if (Offset + Len == BlobSize)
        {
            Flag |= CYW_DL_END;
        }

        Chunk->Flag = (USHORT)Flag;
        Chunk->DloadType = CYW_DL_TYPE_CLM;
        Chunk->Len = Len;
        Chunk->Crc = 0;
        RtlCopyMemory(Chunk->Data, Blob + Offset, Len);

        Status = CywFilIovarSet(Adapter, "clmload", Chunk, HdrSize + Len);
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        Offset += Len;
    }

    CywFree(Chunk);
    CywFree(Blob);
    return Status;
}

NTSTATUS
CywChipDownloadFirmware(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;
    PUCHAR FwImage;
    ULONG FwSize;

    if (Adapter == NULL || Adapter->RamSize == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywReadFile(CYW_FW_DIR CYW_FW_BIN, &FwImage, &FwSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (FwSize < sizeof(Adapter->RstVec) ||
        FwSize > Adapter->RamSize ||
        Adapter->RamBase > MAXULONG - FwSize)
    {
        CywFree(FwImage);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    RtlCopyMemory(&Adapter->RstVec, FwImage, sizeof(Adapter->RstVec));

    Status = CywRamWrite(Adapter, Adapter->RamBase, FwImage, FwSize);
    CywFree(FwImage);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return CywDownloadNvram(Adapter, FwSize);
}

#define CYW_EROM_MAX_DESCRIPTORS 4096

static
ULONG
CywEromDescriptorType(
    _In_ ULONG Descriptor)
{
    ULONG Type = Descriptor & DMP_DESC_TYPE_MSK;

    if ((Type & ~DMP_DESC_ADDRSIZE_GT32) == DMP_DESC_ADDRESS)
    {
        return DMP_DESC_ADDRESS;
    }
    return Type;
}

static
NTSTATUS
CywEromReadDescriptor(
    _In_ PCYW_ADAPTER Adapter,
    _Inout_ PULONG EromAddress,
    _Inout_ PULONG Remaining,
    _Out_ PULONG Descriptor)
{
    NTSTATUS Status;

    if (Adapter == NULL || EromAddress == NULL || Remaining == NULL ||
        Descriptor == NULL || *Remaining == 0 || (*EromAddress & 3) != 0 ||
        *EromAddress > MAXULONG - sizeof(ULONG))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    Status = CywBackplaneReadl(Adapter, *EromAddress, Descriptor);
    if (NT_SUCCESS(Status))
    {
        *EromAddress += sizeof(ULONG);
        (*Remaining)--;
    }
    return Status;
}

static
NTSTATUS
CywEromGetCoreBases(
    _In_ PCYW_ADAPTER Adapter,
    _Inout_ PULONG EromAddress,
    _Inout_ PULONG Remaining,
    _Out_ PULONG RegisterBase,
    _Out_ PULONG WrapperBase)
{
    NTSTATUS Status;
    ULONG Descriptor;
    ULONG DescriptorType;
    ULONG AddressHigh;
    ULONG SizeDescriptor;
    ULONG SizeType;
    ULONG SlaveType;
    ULONG WrapperType;

    *RegisterBase = 0;
    *WrapperBase = 0;

    Status = CywEromReadDescriptor(Adapter, EromAddress, Remaining,
                                   &Descriptor);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    DescriptorType = CywEromDescriptorType(Descriptor);
    if (DescriptorType == DMP_DESC_MASTER_PORT)
    {
        WrapperType = DMP_SLAVE_TYPE_MWRAP;
    }
    else if (DescriptorType == DMP_DESC_ADDRESS)
    {
        *EromAddress -= sizeof(ULONG);
        WrapperType = DMP_SLAVE_TYPE_SWRAP;
    }
    else
    {
        *EromAddress -= sizeof(ULONG);
        return STATUS_NOT_FOUND;
    }

    while (*Remaining != 0)
    {
        do
        {
            Status = CywEromReadDescriptor(Adapter, EromAddress, Remaining,
                                           &Descriptor);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            DescriptorType = CywEromDescriptorType(Descriptor);
            if (DescriptorType == DMP_DESC_EOT)
            {
                *EromAddress -= sizeof(ULONG);
                return STATUS_SUCCESS;
            }
        } while (DescriptorType != DMP_DESC_ADDRESS &&
                 DescriptorType != DMP_DESC_COMPONENT);

        if (DescriptorType == DMP_DESC_COMPONENT)
        {
            *EromAddress -= sizeof(ULONG);
            return STATUS_SUCCESS;
        }

        AddressHigh = 0;
        if (Descriptor & DMP_DESC_ADDRSIZE_GT32)
        {
            Status = CywEromReadDescriptor(Adapter, EromAddress, Remaining,
                                           &AddressHigh);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
        }

        SizeType = (Descriptor & DMP_SLAVE_SIZE_TYPE) >>
                   DMP_SLAVE_SIZE_TYPE_S;
        if (SizeType == DMP_SLAVE_SIZE_DESC)
        {
            Status = CywEromReadDescriptor(Adapter, EromAddress, Remaining,
                                           &SizeDescriptor);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            if (SizeDescriptor & DMP_DESC_ADDRSIZE_GT32)
            {
                Status = CywEromReadDescriptor(Adapter, EromAddress,
                                               Remaining, &SizeDescriptor);
                if (!NT_SUCCESS(Status))
                {
                    return Status;
                }
            }
        }

        if (AddressHigh != 0 ||
            (SizeType != DMP_SLAVE_SIZE_4K &&
             SizeType != DMP_SLAVE_SIZE_8K))
        {
            continue;
        }

        SlaveType = (Descriptor & DMP_SLAVE_TYPE) >> DMP_SLAVE_TYPE_S;
        if (*RegisterBase == 0 && SlaveType == DMP_SLAVE_TYPE_SLAVE)
        {
            *RegisterBase = Descriptor & DMP_SLAVE_ADDR_BASE;
        }
        if (*WrapperBase == 0 && SlaveType == WrapperType)
        {
            *WrapperBase = Descriptor & DMP_SLAVE_ADDR_BASE;
        }
        if (*RegisterBase != 0 && *WrapperBase != 0)
        {
            return STATUS_SUCCESS;
        }
    }

    return STATUS_DEVICE_DATA_ERROR;
}

static
NTSTATUS
CywChipEnumerateCores(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG EromAddress;
    ULONG Remaining = CYW_EROM_MAX_DESCRIPTORS;
    ULONG Descriptor;
    ULONG DescriptorB;
    ULONG DescriptorType;
    ULONG CoreId;
    ULONG MasterWrappers;
    ULONG SlaveWrappers;
    ULONG RegisterBase;
    ULONG WrapperBase;
    BOOLEAN FoundEnd = FALSE;

    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter->Cr4WrapBase = 0;
    Adapter->SdioCoreBase = 0;

    Status = CywBackplaneReadl(Adapter, SI_ENUM_BASE_DEFAULT + CC_EROMPTR,
                               &EromAddress);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (EromAddress == 0 || (EromAddress & 3) != 0)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    while (Remaining != 0)
    {
        Status = CywEromReadDescriptor(Adapter, &EromAddress, &Remaining,
                                       &Descriptor);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        DescriptorType = CywEromDescriptorType(Descriptor);
        if (DescriptorType == DMP_DESC_EOT)
        {
            FoundEnd = TRUE;
            break;
        }
        if (!(Descriptor & DMP_DESC_VALID) ||
            DescriptorType == DMP_DESC_EMPTY ||
            DescriptorType != DMP_DESC_COMPONENT)
        {
            continue;
        }

        CoreId = (Descriptor & DMP_COMP_PARTNUM) >> DMP_COMP_PARTNUM_S;
        Status = CywEromReadDescriptor(Adapter, &EromAddress, &Remaining,
                                       &DescriptorB);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (!(DescriptorB & DMP_DESC_VALID) ||
            CywEromDescriptorType(DescriptorB) != DMP_DESC_COMPONENT)
        {
            return STATUS_DEVICE_DATA_ERROR;
        }

        MasterWrappers = (DescriptorB & DMP_COMP_NUM_MWRAP) >>
                         DMP_COMP_NUM_MWRAP_S;
        SlaveWrappers = (DescriptorB & DMP_COMP_NUM_SWRAP) >>
                        DMP_COMP_NUM_SWRAP_S;
        if (MasterWrappers + SlaveWrappers == 0)
        {
            continue;
        }

        Status = CywEromGetCoreBases(Adapter, &EromAddress, &Remaining,
                                     &RegisterBase, &WrapperBase);
        if (Status == STATUS_NOT_FOUND)
        {
            continue;
        }
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (CoreId == BCMA_CORE_ARM_CR4)
        {
            if (WrapperBase == 0 || Adapter->Cr4WrapBase != 0)
            {
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            Adapter->Cr4WrapBase = WrapperBase;
        }
        else if (CoreId == BCMA_CORE_SDIO_DEV)
        {
            if (RegisterBase == 0 || Adapter->SdioCoreBase != 0)
            {
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            Adapter->SdioCoreBase = RegisterBase;
        }
    }

    if (!FoundEnd || Adapter->Cr4WrapBase == 0 ||
        Adapter->SdioCoreBase == 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywChipDisableCore(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG WrapBase,
    _In_ ULONG Prereset,
    _In_ ULONG Reset)
{
    NTSTATUS Status;
    ULONG Value;
    ULONG Count;

    if (Adapter == NULL || WrapBase == 0 ||
        WrapBase > MAXULONG - BCMA_RESET_CTL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywBackplaneReadl(Adapter, WrapBase + BCMA_RESET_CTL, &Value);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!(Value & BCMA_RESET_CTL_RESET))
    {
        Status = CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL,
                                    Prereset | SICF_FGC | SICF_CLOCK_EN);
        if (NT_SUCCESS(Status))
        {
            Status = CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL,
                                       &Value);
        }
        if (NT_SUCCESS(Status))
        {
            Status = CywBackplaneWritel(Adapter, WrapBase + BCMA_RESET_CTL,
                                        BCMA_RESET_CTL_RESET);
        }
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        KeStallExecutionProcessor(10);
        for (Count = 0; Count < 30; Count++)
        {
            Status = CywBackplaneReadl(Adapter,
                                       WrapBase + BCMA_RESET_CTL, &Value);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            if (Value & BCMA_RESET_CTL_RESET)
            {
                break;
            }
            KeStallExecutionProcessor(10);
        }
        if (!(Value & BCMA_RESET_CTL_RESET))
        {
            return STATUS_DEVICE_NOT_READY;
        }
    }

    Status = CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL,
                                Reset | SICF_FGC | SICF_CLOCK_EN);
    if (NT_SUCCESS(Status))
    {
        Status = CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL, &Value);
    }
    return Status;
}

static
NTSTATUS
CywChipResetCore(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG WrapBase,
    _In_ ULONG Prereset,
    _In_ ULONG Reset,
    _In_ ULONG Postreset)
{
    NTSTATUS Status;
    ULONG Value;
    ULONG Count;

    Status = CywChipDisableCore(Adapter, WrapBase, Prereset, Reset);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Count = 0; Count <= 50; Count++)
    {
        Status = CywBackplaneReadl(Adapter, WrapBase + BCMA_RESET_CTL,
                                   &Value);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (!(Value & BCMA_RESET_CTL_RESET))
        {
            break;
        }
        if (Count == 50)
        {
            return STATUS_DEVICE_NOT_READY;
        }

        Status = CywBackplaneWritel(Adapter, WrapBase + BCMA_RESET_CTL, 0);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        KeStallExecutionProcessor(50);
    }

    Status = CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL,
                                Postreset | SICF_CLOCK_EN);
    if (NT_SUCCESS(Status))
    {
        Status = CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL, &Value);
    }
    if (NT_SUCCESS(Status))
    {
        KeStallExecutionProcessor(10);
    }
    return Status;
}

static
NTSTATUS
CywChipSetActive(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Rstvec)
{
    NTSTATUS Status;

    if (Adapter == NULL || Adapter->ControlBuffer == NULL ||
        Adapter->Cr4WrapBase == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ((PULONG)Adapter->ControlBuffer)[0] = Rstvec;
    Status = CywRamWrite(Adapter, 0, Adapter->ControlBuffer, sizeof(Rstvec));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return CywChipResetCore(Adapter, Adapter->Cr4WrapBase,
                            ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);
}

static
BOOLEAN
CywIsValidEthernetAddress(
    _In_reads_(CYW_ADDRESS_LENGTH) const UCHAR *Address)
{
    UCHAR Nonzero = 0;
    ULONG Index;

    if (Address == NULL || (Address[0] & 1) != 0)
    {
        return FALSE;
    }

    for (Index = 0; Index < CYW_ADDRESS_LENGTH; Index++)
    {
        Nonzero |= Address[Index];
    }
    return Nonzero != 0;
}

static
NTSTATUS
CywSetCountry(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCSTR Alpha2)
{
    CYW_COUNTRY_LE Cc;

    if (Adapter == NULL || Alpha2 == NULL ||
        Alpha2[0] < 'A' || Alpha2[0] > 'Z' ||
        Alpha2[1] < 'A' || Alpha2[1] > 'Z' || Alpha2[2] != ANSI_NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Cc, sizeof(Cc));
    Cc.Rev = -1;
    Cc.CountryAbbrev[0] = Alpha2[0];
    Cc.CountryAbbrev[1] = Alpha2[1];
    Cc.Ccode[0] = Alpha2[0];
    Cc.Ccode[1] = Alpha2[1];
    return CywFilIovarSet(Adapter, "country", &Cc, sizeof(Cc));
}

NTSTATUS
CywChipBringUp(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;
    UCHAR InterruptEnable;

    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywSdioSetBlockSize(Adapter, CYW_SDIO_FUNC_BACKPLANE, CYW_F1_BLOCKSIZE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywSdioEnableFunction(Adapter, CYW_SDIO_FUNC_BACKPLANE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywChipRecognize(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Adapter->RamSize = CYW43455_RAMSIZE;

    Status = CywChipEnumerateCores(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    {
        ULONG Cr4Ioctl;

        Status = CywBackplaneReadl(Adapter,
                                   Adapter->Cr4WrapBase + BCMA_IOCTL,
                                   &Cr4Ioctl);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Cr4Ioctl &= ARMCR4_BCMA_IOCTL_CPUHALT;
        Status = CywChipResetCore(Adapter, Adapter->Cr4WrapBase,
                                  Cr4Ioctl,
                                  ARMCR4_BCMA_IOCTL_CPUHALT,
                                  ARMCR4_BCMA_IOCTL_CPUHALT);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    Status = CywChipDownloadFirmware(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywChipSetActive(Adapter, Adapter->RstVec);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywClockRequest(Adapter, SBSDIO_HT_AVAIL_REQ,
                             SBSDIO_AVAIL_MASK, 1000);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    {
        UCHAR Devctl;

        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                 SBSDIO_DEVICE_CTL, &Devctl);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Devctl |= SBSDIO_DEVCTL_F2WM_ENAB;
        Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                  SBSDIO_DEVICE_CTL, Devctl);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                              SBSDIO_FUNC1_WATERMARK,
                              CY_43455_F2_WATERMARK);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                              SBSDIO_FUNC1_MESBUSYCTRL,
                              CY_43455_MES_WATERMARK |
                              SBSDIO_MESBUSYCTRL_ENAB);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywSdioSetBlockSize(Adapter, CYW_SDIO_FUNC_RADIO, CYW_F2_BLOCKSIZE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywBackplaneWritel(Adapter,
                               Adapter->SdioCoreBase +
                               SD_REG_TOSBMAILBOXDATA,
                               SMB_DATA_VERSION);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywSdioEnableFunction(Adapter, CYW_SDIO_FUNC_RADIO);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                             SDIO_CCCR_INTEN, &InterruptEnable);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                              SDIO_CCCR_INTEN,
                              InterruptEnable | SDIO_INTR_ENABLE_MASTER |
                              SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywBackplaneWritel(Adapter,
                               Adapter->SdioCoreBase + SD_REG_HOSTINTMASK,
                               CYW_HOSTINTMASK);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywDownloadClm(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    {
        ULONG Down = 0;

        Status = CywFilCmdSet(Adapter, BRCMF_C_DOWN, &Down, sizeof(Down));
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    Status = CywSetCountry(Adapter, CYW_DEFAULT_COUNTRY);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    {
        ULONG GlomVal = 0xFFFFFFFF;

        Status = CywFilIovarSetInt(Adapter, "ampdu_rx", 1);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CYW: optional ampdu_rx setup failed 0x%08lx\n", Status);
        }
        Status = CywFilIovarSetInt(Adapter, "bus:txglom", 1);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CYW: optional txglom setup failed 0x%08lx\n", Status);
        }
        else if (NT_SUCCESS(CywFilIovarGet(Adapter, "bus:txglom",
                                           &GlomVal, sizeof(GlomVal))) &&
                 GlomVal == 1)
        {
            Status = CywFilIovarSetInt(Adapter, "bus:txglomalign", 4);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("CYW: optional txglom alignment failed 0x%08lx\n", Status);
            }
        }
    }

    Status = CywFilIovarSetInt(Adapter, "mpc", 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: optional mpc setup failed 0x%08lx\n", Status);
    }

    {
        ULONG Bw[2];

        Bw[0] = 2; Bw[1] = 0x3;
        Status = CywFilIovarSet(Adapter, "bw_cap", Bw, sizeof(Bw));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CYW: optional 2.4 GHz bandwidth setup failed 0x%08lx\n", Status);
        }
        Bw[0] = 1; Bw[1] = 0x7;
        Status = CywFilIovarSet(Adapter, "bw_cap", Bw, sizeof(Bw));
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CYW: optional 5 GHz bandwidth setup failed 0x%08lx\n", Status);
        }
    }

    {
        ULONG Infra = 1;
        ULONG Up = 0;

        Status = CywFilCmdSet(Adapter, BRCMF_C_SET_INFRA,
                              &Infra, sizeof(Infra));
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Status = CywFilCmdSet(Adapter, BRCMF_C_UP, &Up, sizeof(Up));
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    Status = CywActivateEvents(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywFilIovarGet(Adapter, "cur_etheraddr",
                            Adapter->CurrentAddress, CYW_ADDRESS_LENGTH);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (!CywIsValidEthernetAddress(Adapter->CurrentAddress))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }
    RtlCopyMemory(Adapter->PermanentAddress, Adapter->CurrentAddress, CYW_ADDRESS_LENGTH);

    return STATUS_SUCCESS;
}
