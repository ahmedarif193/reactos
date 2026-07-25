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
    Data = CywAllocate(ALIGN_UP(Length, ULONG));
    if (Data == NULL)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Data, Length, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        CywFree(Data);
        return Status;
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
    _In_ UCHAR AvailMask)
{
    NTSTATUS Status;
    UCHAR Csr;
    ULONG Retry;

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                             SBSDIO_FUNC1_CHIPCLKCSR, &Csr);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Csr |= Request;
    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                              SBSDIO_FUNC1_CHIPCLKCSR, Csr);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (AvailMask == 0)
    {
        return STATUS_SUCCESS;
    }

    for (Retry = 0; Retry < 5000; Retry++)
    {
        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                 SBSDIO_FUNC1_CHIPCLKCSR, &Csr);
        if (NT_SUCCESS(Status) && (Csr & AvailMask))
        {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(200);
    }

    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
CywChipRecognize(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG RegData;

    CywClockRequest(Adapter, SBSDIO_ALP_AVAIL_REQ, SBSDIO_ALP_AVAIL);

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
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;
    PUCHAR Raw;
    PUCHAR Stripped;
    ULONG RawSize;
    ULONG OutLen;
    ULONG Words;
    ULONG Token;
    ULONG i;
    ULONG j;

    Status = CywReadFile(CYW_FW_DIR CYW_FW_NVRAM, &Raw, &RawSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Stripped = CywAllocate(RawSize + 8);
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
    if (OutLen == 0 || Stripped[OutLen - 1] != '\0')
    {
        Stripped[OutLen++] = '\0';
    }

    while ((OutLen % 4) != 0)
    {
        Stripped[OutLen++] = '\0';
    }

    Words = OutLen / 4;
    Token = CYW_NVRAM_TOKEN(Words);

    j = Adapter->RamBase + Adapter->RamSize - OutLen - 4;
    Status = CywRamWrite(Adapter, j, Stripped, OutLen);
    if (NT_SUCCESS(Status))
    {
        ((PULONG)Adapter->ControlBuffer)[0] = Token;
        Status = CywRamWrite(Adapter, j + OutLen, Adapter->ControlBuffer, sizeof(Token));
    }

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

    Status = CywReadFile(CYW_FW_DIR CYW_FW_CLM, &Blob, &BlobSize);
    if (!NT_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
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

    Status = CywReadFile(CYW_FW_DIR CYW_FW_BIN, &FwImage, &FwSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Adapter->RstVec = ((PULONG)FwImage)[0];

    Status = CywRamWrite(Adapter, Adapter->RamBase, FwImage, FwSize);
    CywFree(FwImage);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Adapter->RamSize > 0)
    {
        Status = CywDownloadNvram(Adapter);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
CywChipEnumerateCores(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG EromAddr;
    ULONG Desc;
    ULONG DescB;
    ULONG CoreId;
    ULONG Nmp;
    ULONG i;
    ULONG RegBase;
    ULONG WrapBase;

    Status = CywBackplaneReadl(Adapter, SI_ENUM_BASE_DEFAULT + CC_EROMPTR, &EromAddr);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (;;)
    {
        Status = CywBackplaneReadl(Adapter, EromAddr, &Desc);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        EromAddr += 4;

        if ((Desc & DMP_DESC_TYPE_MSK) == DMP_DESC_EOT)
        {
            break;
        }
        if ((Desc & DMP_DESC_TYPE_MSK) != DMP_DESC_COMPONENT)
        {
            continue;
        }

        CoreId = (Desc & DMP_COMP_PARTNUM) >> DMP_COMP_PARTNUM_S;

        Status = CywBackplaneReadl(Adapter, EromAddr, &DescB);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        EromAddr += 4;
        Nmp = (DescB & DMP_COMP_NUM_MPORT) >> DMP_COMP_NUM_MPORT_S;

        for (i = 0; i < Nmp; i++)
        {
            EromAddr += 4;
        }

        RegBase = 0;
        WrapBase = 0;
        for (;;)
        {
            ULONG DescType;
            ULONG SizeType;
            ULONG SlaveType;

            Status = CywBackplaneReadl(Adapter, EromAddr, &Desc);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }

            DescType = Desc & DMP_DESC_TYPE_MSK;
            if (DescType != DMP_DESC_ADDRESS &&
                DescType != (DMP_DESC_ADDRESS | DMP_DESC_ADDRSIZE_GT32))
            {
                break;
            }
            EromAddr += 4;
            if (DescType & DMP_DESC_ADDRSIZE_GT32)
            {
                EromAddr += 4;
            }

            SizeType = (Desc & DMP_SLAVE_SIZE_TYPE) >> DMP_SLAVE_SIZE_TYPE_S;
            if (SizeType == DMP_SLAVE_SIZE_DESC)
            {
                ULONG SizeDesc = 0;
                CywBackplaneReadl(Adapter, EromAddr, &SizeDesc);
                EromAddr += 4;
                if (SizeDesc & DMP_DESC_ADDRSIZE_GT32)
                {
                    EromAddr += 4;
                }
                continue;
            }
            if (SizeType != DMP_SLAVE_SIZE_4K && SizeType != DMP_SLAVE_SIZE_8K)
            {
                continue;
            }

            SlaveType = (Desc & DMP_SLAVE_TYPE) >> DMP_SLAVE_TYPE_S;
            if (SlaveType == DMP_SLAVE_TYPE_SLAVE)
            {
                if (RegBase == 0)
                {
                    RegBase = Desc & DMP_SLAVE_ADDR_BASE;
                }
            }
            else if (SlaveType == DMP_SLAVE_TYPE_MWRAP ||
                     SlaveType == DMP_SLAVE_TYPE_SWRAP)
            {
                if (WrapBase == 0)
                {
                    WrapBase = Desc & DMP_SLAVE_ADDR_BASE;
                }
            }
        }

        if (CoreId == BCMA_CORE_ARM_CR4)
        {
            Adapter->Cr4WrapBase = WrapBase;
        }

        if (CoreId == BCMA_CORE_SDIO_DEV)
        {
            Adapter->SdioCoreBase = RegBase;
        }
    }

    if (Adapter->Cr4WrapBase == 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static
VOID
CywChipResetCore(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG WrapBase,
    _In_ ULONG Prereset,
    _In_ ULONG Reset,
    _In_ ULONG Postreset)
{
    ULONG Val = 0;
    ULONG Count;

    CywBackplaneReadl(Adapter, WrapBase + BCMA_RESET_CTL, &Val);
    if (!(Val & BCMA_RESET_CTL_RESET))
    {
        CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL, Prereset | SICF_FGC | SICF_CLOCK_EN);
        CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL, &Val);

        CywBackplaneWritel(Adapter, WrapBase + BCMA_RESET_CTL, BCMA_RESET_CTL_RESET);
        CywBackplaneReadl(Adapter, WrapBase + BCMA_RESET_CTL, &Val);
        KeStallExecutionProcessor(10);

        CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL, Reset | SICF_FGC | SICF_CLOCK_EN);
        CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL, &Val);
    }

    Count = 0;
    for (;;)
    {
        CywBackplaneReadl(Adapter, WrapBase + BCMA_RESET_CTL, &Val);
        if (!(Val & BCMA_RESET_CTL_RESET))
        {
            break;
        }
        CywBackplaneWritel(Adapter, WrapBase + BCMA_RESET_CTL, 0);
        CywBackplaneReadl(Adapter, WrapBase + BCMA_RESET_CTL, &Val);
        KeStallExecutionProcessor(10);
        if (++Count >= 50)
        {
            break;
        }
    }

    CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL, Postreset | SICF_CLOCK_EN);
    CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL, &Val);
    KeStallExecutionProcessor(10);
}

static
VOID
CywChipDisableCore(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG WrapBase,
    _In_ ULONG Prereset,
    _In_ ULONG Reset)
{
    ULONG Val = 0;

    CywBackplaneReadl(Adapter, WrapBase + BCMA_RESET_CTL, &Val);
    if (Val & BCMA_RESET_CTL_RESET)
    {
        CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL, Reset | SICF_FGC | SICF_CLOCK_EN);
        CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL, &Val);
        return;
    }

    CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL, Prereset | SICF_FGC | SICF_CLOCK_EN);
    CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL, &Val);

    CywBackplaneWritel(Adapter, WrapBase + BCMA_RESET_CTL, BCMA_RESET_CTL_RESET);
    CywBackplaneReadl(Adapter, WrapBase + BCMA_RESET_CTL, &Val);
    KeStallExecutionProcessor(10);

    CywBackplaneWritel(Adapter, WrapBase + BCMA_IOCTL, Reset | SICF_FGC | SICF_CLOCK_EN);
    CywBackplaneReadl(Adapter, WrapBase + BCMA_IOCTL, &Val);
    KeStallExecutionProcessor(1);
}

static
VOID
CywChipSetActive(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Rstvec)
{
    ((PULONG)Adapter->ControlBuffer)[0] = Rstvec;
    CywRamWrite(Adapter, 0, Adapter->ControlBuffer, 4);

    CywChipResetCore(Adapter, Adapter->Cr4WrapBase, ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);
}

static
NTSTATUS
CywSetCountry(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCSTR Alpha2)
{
    CYW_COUNTRY_LE Cc;

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
    ULONG IntStatus;

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
        CywBackplaneReadl(Adapter, Adapter->Cr4WrapBase + BCMA_IOCTL, &Cr4Ioctl);
        Cr4Ioctl |= ARMCR4_BCMA_IOCTL_CPUHALT;
        CywBackplaneWritel(Adapter, Adapter->Cr4WrapBase + BCMA_IOCTL, Cr4Ioctl);
    }

    Status = CywChipDownloadFirmware(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    CywChipSetActive(Adapter, Adapter->RstVec);

    CywClockRequest(Adapter, SBSDIO_HT_AVAIL_REQ, SBSDIO_HT_AVAIL);

    {
        UCHAR Devctl = 0;
        CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BACKPLANE, SBSDIO_DEVICE_CTL, &Devctl);
        Devctl |= SBSDIO_DEVCTL_F2WM_ENAB;
        CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE, SBSDIO_DEVICE_CTL, Devctl);
    }
    CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE, SBSDIO_FUNC1_WATERMARK,
                     CY_43455_F2_WATERMARK);
    CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE, SBSDIO_FUNC1_MESBUSYCTRL,
                     CY_43455_MES_WATERMARK | SBSDIO_MESBUSYCTRL_ENAB);

    Status = CywSdioSetBlockSize(Adapter, CYW_SDIO_FUNC_RADIO, CYW_F2_BLOCKSIZE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Adapter->SdioCoreBase != 0)
    {
        CywBackplaneWritel(Adapter, Adapter->SdioCoreBase + SD_REG_TOSBMAILBOXDATA,
                           SMB_DATA_VERSION);
    }

    Status = CywSdioEnableFunction(Adapter, CYW_SDIO_FUNC_RADIO);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_INTEN, (PUCHAR)&IntStatus);
    if (NT_SUCCESS(Status))
    {
        CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_INTEN,
                         (UCHAR)(IntStatus | SDIO_INTR_ENABLE_MASTER |
                                 SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2));
    }

    if (Adapter->SdioCoreBase != 0)
    {
        CywBackplaneWritel(Adapter, Adapter->SdioCoreBase + SD_REG_HOSTINTMASK,
                           CYW_HOSTINTMASK);
    }

    CywDownloadClm(Adapter);

    {
        ULONG Down = 0;
        NTSTATUS DownStatus = CywFilCmdSet(Adapter, BRCMF_C_DOWN, &Down, sizeof(Down));
        if (!NT_SUCCESS(DownStatus))
        {
            DPRINT1("CYW: BRCMF_C_DOWN failed 0x%08lx\n", DownStatus);
        }
    }

    CywSetCountry(Adapter, CYW_DEFAULT_COUNTRY);

    {
        ULONG GlomVal = 0xFFFFFFFF;

        CywFilIovarSetInt(Adapter, "ampdu_rx", 1);
        CywFilIovarSetInt(Adapter, "bus:txglom", 1);
        if (NT_SUCCESS(CywFilIovarGet(Adapter, "bus:txglom", &GlomVal, sizeof(GlomVal))) && GlomVal == 1)
        {
            CywFilIovarSetInt(Adapter, "bus:txglomalign", 4);
        }
    }


    CywFilIovarSetInt(Adapter, "mpc", 0);

    {
        ULONG Bw[2];
        Bw[0] = 2; Bw[1] = 0x3;
        CywFilIovarSet(Adapter, "bw_cap", Bw, sizeof(Bw));
        Bw[0] = 1; Bw[1] = 0x7;
        CywFilIovarSet(Adapter, "bw_cap", Bw, sizeof(Bw));
    }

    {
        ULONG Infra = 1;
        ULONG Up = 0;
        CywFilCmdSet(Adapter, BRCMF_C_SET_INFRA, &Infra, sizeof(Infra));
        Status = CywFilCmdSet(Adapter, BRCMF_C_UP, &Up, sizeof(Up));
    }

    CywActivateEvents(Adapter);

    CywFilIovarGet(Adapter, "cur_etheraddr", Adapter->CurrentAddress, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(Adapter->PermanentAddress, Adapter->CurrentAddress, CYW_ADDRESS_LENGTH);

    return STATUS_SUCCESS;
}
