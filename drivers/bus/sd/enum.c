/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Card enumeration and initialization sequences
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

#define SDIO_OCR_READY                 0x80000000
#define SDIO_OCR_NUM_FUNCTIONS_MASK    0x70000000
#define SDIO_OCR_NUM_FUNCTIONS_SHIFT   28
#define SDIO_OCR_MEMORY_PRESENT        0x08000000

#define SD_VOLTAGE_SWITCH_REGULATOR_MS   5
#define SD_VOLTAGE_SWITCH_CLOCK_SETTLE_MS 1
#define SD_VOLTAGE_SWITCH_DAT_POLL_MS    2

static NTSTATUS
SdBusWaitForBusyRelease(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex)
{
    ULONG Timeout;

    Timeout = SD_DATA_TIMEOUT_MS * 100;
    while (Timeout > 0)
    {
        if (!(SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE) &
              SDHCI_PS_DATA_INHIBIT))
        {
            return STATUS_SUCCESS;
        }

        KeStallExecutionProcessor(10);
        Timeout--;
    }

    DPRINT1("SdBusWaitForBusyRelease: CMD%u timed out waiting for busy release\n",
            CommandIndex);
    (void)SdBusResetHost(FdoExtension, SDHCI_RESET_DATA);
    return STATUS_IO_TIMEOUT;
}

/**
 * @brief Issue a single SD/MMC command via SDHCI registers (no data transfer).
 *
 * Waits for CMD_INHIBIT to clear, writes the argument and command registers,
 * polls for command completion or error, and optionally reads the response.
 * For R2 (136-bit) responses, the caller must provide a 4-element ULONG array.
 *
 * @param[in]      FdoExtension   Pointer to the FDO device extension.
 * @param[in]      CommandIndex   SD command index (0-63).
 * @param[in]      Argument       32-bit command argument.
 * @param[in]      CommandFlags   SDHCI command register flags (response type, CRC, index check).
 * @param[out]     Response       Optional pointer to receive the response (1 or 4 ULONGs).
 *
 * @return STATUS_SUCCESS, STATUS_IO_TIMEOUT, STATUS_SD_CMD_TIMEOUT, or
 *         STATUS_SD_CMD_CRC_ERROR.
 */
NTSTATUS
SdBusSendCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response)
{
    USHORT CmdReg;
    ULONG CmdBits;
    NTSTATUS WaitStatus;
    ULONG Timeout;

    /* Wait for CMD_INHIBIT to clear (hardware readiness, not completion) */
    Timeout = SD_CMD_TIMEOUT_MS * 100;
    while (Timeout > 0)
    {
        if (!(SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE) & SDHCI_PS_CMD_INHIBIT))
        {
            break;
        }
        KeStallExecutionProcessor(10);
        Timeout--;
    }

    if (Timeout == 0)
    {
        DPRINT1("SdBusSendCommand: CMD%u timed out waiting for CMD_INHIBIT\n",
                CommandIndex);
        return STATUS_IO_TIMEOUT;
    }

    /* Prepare for interrupt-driven completion */
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    KeClearEvent(&FdoExtension->CommandEvent);
    SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);

    /* Write argument */
    SdBusWriteReg32(FdoExtension, SDHCI_ARGUMENT, Argument);

    /* Build and write command register */
    CmdReg = SDHCI_MAKE_CMD(CommandIndex, CommandFlags);
    SdBusWriteReg16(FdoExtension, SDHCI_COMMAND, CmdReg);

    /* Wait for command complete via interrupt */
    WaitStatus = SdBusWaitForInterrupt(
        FdoExtension,
        SDHCI_INT_CMD_COMPLETE,
        SDHCI_INT_CMD_ERROR_MASK,
        SD_CMD_TIMEOUT_MS,
        &CmdBits);

    if (WaitStatus == STATUS_IO_DEVICE_ERROR)
    {
        (void)SdBusResetHost(FdoExtension, SDHCI_RESET_CMD);
        if (CmdBits & SDHCI_INT_CMD_TIMEOUT)
        {
            return STATUS_SD_CMD_TIMEOUT;
        }
        return STATUS_SD_CMD_CRC_ERROR;
    }

    if (WaitStatus == STATUS_IO_TIMEOUT)
    {
        DPRINT1("SdBusSendCommand: CMD%u completion timed out\n", CommandIndex);
        (void)SdBusResetHost(FdoExtension, SDHCI_RESET_CMD);
        return STATUS_IO_TIMEOUT;
    }

    /* Read response if requested */
    if (Response != NULL)
    {
        if (CommandFlags & SDHCI_CMD_RESP_136)
        {
            Response[0] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE0);
            Response[1] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE1);
            Response[2] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE2);
            Response[3] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE3);
        }
        else
        {
            Response[0] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE0);
        }
    }

    if (CommandFlags & SDHCI_CMD_RESP_48_BUSY)
    {
        return SdBusWaitForBusyRelease(FdoExtension, CommandIndex);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Send an application-specific command (CMD55 + ACMDxx).
 *
 * Issues CMD55 (APP_CMD) with the given RCA to switch the card into
 * application-command mode, then sends the actual application command.
 *
 * @param[in]      FdoExtension   Pointer to the FDO device extension.
 * @param[in]      Rca            Relative card address (0 during identification).
 * @param[in]      CommandIndex   Application command index to send after CMD55.
 * @param[in]      Argument       32-bit argument for the application command.
 * @param[in]      CommandFlags   SDHCI command register flags for the application command.
 * @param[out]     Response       Optional pointer to receive the response.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdBusSendAppCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Rca,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response)
{
    NTSTATUS Status;
    ULONG AppCmdResp;

    /* Send CMD55 (APP_CMD) first */
    Status = SdBusSendCommand(FdoExtension,
                              SDCMD_APP_CMD,
                              Rca << 16,
                              SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                              &AppCmdResp);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusSendAppCommand: CMD55 failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Now send the actual application command */
    Status = SdBusSendCommand(FdoExtension,
                              CommandIndex,
                              Argument,
                              CommandFlags,
                              Response);
    return Status;
}

static NTSTATUS
SdBusTryMmcOpCond(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PULONG Ocr,
    _Out_ PBOOLEAN IsHighCapacity)
{
    ULONG Response;
    ULONG Timeout;
    NTSTATUS Status;
    LARGE_INTEGER Delay;

    Delay.QuadPart = -10000LL;
    Timeout = SD_INIT_TIMEOUT_MS;

    do
    {
        Status = SdBusSendCommand(FdoExtension,
                                  SDCMD_SEND_OP_COND,
                                  SD_OCR_VDD_RANGE | MMC_OCR_SECTOR_MODE,
                                  SDHCI_CMD_RESP_48,
                                  &Response);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        *Ocr = Response;
        if (Response & MMC_OCR_BUSY)
        {
            *IsHighCapacity = (Response & MMC_OCR_SECTOR_MODE) ? TRUE : FALSE;
            return STATUS_SUCCESS;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        Timeout--;
    } while (Timeout > 0);

    return STATUS_IO_TIMEOUT;
}

/**
 * @brief Parse a Card Identification (CID) register from SDHCI R2 response data.
 *
 * Extracts the manufacturer ID, OEM ID, product name, revision, serial
 * number, manufacturing date, and CRC from the 128-bit response stored
 * across four SDHCI response registers.
 *
 * @param[in]  RawResponse  Pointer to a 4-element ULONG array containing the R2 response.
 * @param[out] Cid          Pointer to an SD_CID structure to populate.
 */
VOID
SdBusParseCid(
    _In_ PULONG RawResponse,
    _Out_ PSD_CID Cid)
{
    /*
     * SDHCI response registers for R2 are stored in a somewhat shifted
     * fashion. The 128-bit CID is spread across Response[3..0] with the
     * MSB in Response[3] bits [23:0] (CID[127:104]) and the internal CRC
     * stripped by hardware. We parse the fields from the raw 128-bit value.
     *
     * Layout in SDHCI response registers (per SDHCI spec):
     *   RESPONSE3[23:0]  = CID[127:104]  (MID, OID high)
     *   RESPONSE2[31:0]  = CID[103:72]   (OID low, PNM high)
     *   RESPONSE1[31:0]  = CID[71:40]    (PNM low, PRV, PSN high)
     *   RESPONSE0[31:0]  = CID[39:8]     (PSN low, MDT, CRC)
     */
    RtlZeroMemory(Cid, sizeof(SD_CID));

    Cid->ManufacturerId = (UCHAR)((RawResponse[3] >> 16) & 0xFF);
    Cid->OemId = (USHORT)((RawResponse[3] >> 0) & 0xFFFF);

    Cid->ProductName[0] = (UCHAR)((RawResponse[2] >> 24) & 0xFF);
    Cid->ProductName[1] = (UCHAR)((RawResponse[2] >> 16) & 0xFF);
    Cid->ProductName[2] = (UCHAR)((RawResponse[2] >> 8) & 0xFF);
    Cid->ProductName[3] = (UCHAR)((RawResponse[2] >> 0) & 0xFF);
    Cid->ProductName[4] = (UCHAR)((RawResponse[1] >> 24) & 0xFF);

    Cid->ProductRevision = (UCHAR)((RawResponse[1] >> 16) & 0xFF);
    Cid->ProductSerialNumber = ((RawResponse[1] & 0xFFFF) << 16) |
                                ((RawResponse[0] >> 16) & 0xFFFF);
    Cid->ManufacturingDate = (USHORT)((RawResponse[0] >> 4) & 0xFFF);
    Cid->Crc7 = (UCHAR)((RawResponse[0] << 1) & 0xFE);
}

/**
 * @brief Parse a Card-Specific Data (CSD) register from SDHCI R2 response data.
 *
 * Determines CSD version (v1 for SDSC, v2 for SDHC/SDXC) and extracts
 * timing, capacity, and configuration fields. Raw response data is also
 * preserved in the Csd->Raw array.
 *
 * @param[in]  RawResponse  Pointer to a 4-element ULONG array containing the R2 response.
 * @param[out] Csd          Pointer to an SD_CSD structure to populate.
 */
VOID
SdBusParseCsd(
    _In_ PULONG RawResponse,
    _Out_ PSD_CSD Csd)
{
    RtlZeroMemory(Csd, sizeof(SD_CSD));

    /* Save raw data */
    Csd->Raw[0] = RawResponse[0];
    Csd->Raw[1] = RawResponse[1];
    Csd->Raw[2] = RawResponse[2];
    Csd->Raw[3] = RawResponse[3];

    /* CSD structure version is in bits [127:126] -> Response3[23:22] */
    Csd->CsdVersion = (UCHAR)((RawResponse[3] >> 22) & 0x03);

    if (Csd->CsdVersion == 0)
    {
        /* CSD v1 -- standard capacity */
        Csd->V1.CsdStructure = 0;
        Csd->V1.Taac = (UCHAR)((RawResponse[3] >> 8) & 0xFF);
        Csd->V1.Nsac = (UCHAR)((RawResponse[3] >> 0) & 0xFF);
        Csd->V1.TranSpeed = (UCHAR)((RawResponse[2] >> 24) & 0xFF);
        Csd->V1.Ccc = (USHORT)((RawResponse[2] >> 12) & 0xFFF);
        Csd->V1.ReadBlLen = (UCHAR)((RawResponse[2] >> 8) & 0x0F);
        Csd->V1.ReadBlPartial = (BOOLEAN)((RawResponse[2] >> 7) & 0x01);
        Csd->V1.WriteBlkMisalign = (BOOLEAN)((RawResponse[2] >> 6) & 0x01);
        Csd->V1.ReadBlkMisalign = (BOOLEAN)((RawResponse[2] >> 5) & 0x01);
        Csd->V1.DsrImp = (BOOLEAN)((RawResponse[2] >> 4) & 0x01);
        Csd->V1.CSize = (USHORT)(((RawResponse[2] & 0x03) << 10) |
                                  ((RawResponse[1] >> 22) & 0x3FF));
        Csd->V1.VddRCurrMin = (UCHAR)((RawResponse[1] >> 19) & 0x07);
        Csd->V1.VddRCurrMax = (UCHAR)((RawResponse[1] >> 16) & 0x07);
        Csd->V1.VddWCurrMin = (UCHAR)((RawResponse[1] >> 13) & 0x07);
        Csd->V1.VddWCurrMax = (UCHAR)((RawResponse[1] >> 10) & 0x07);
        Csd->V1.CSizeMult = (UCHAR)((RawResponse[1] >> 7) & 0x07);
    }
    else
    {
        /* CSD v2 -- high/extended capacity */
        Csd->V2.CsdStructure = 1;
        Csd->V2.Taac = (UCHAR)((RawResponse[3] >> 8) & 0xFF);
        Csd->V2.Nsac = (UCHAR)((RawResponse[3] >> 0) & 0xFF);
        Csd->V2.TranSpeed = (UCHAR)((RawResponse[2] >> 24) & 0xFF);
        Csd->V2.Ccc = (USHORT)((RawResponse[2] >> 12) & 0xFFF);
        Csd->V2.ReadBlLen = (UCHAR)((RawResponse[2] >> 8) & 0x0F);
        Csd->V2.ReadBlPartial = (BOOLEAN)((RawResponse[2] >> 7) & 0x01);
        Csd->V2.WriteBlkMisalign = (BOOLEAN)((RawResponse[2] >> 6) & 0x01);
        Csd->V2.ReadBlkMisalign = (BOOLEAN)((RawResponse[2] >> 5) & 0x01);
        Csd->V2.DsrImp = (BOOLEAN)((RawResponse[2] >> 4) & 0x01);
        /* C_SIZE for CSD v2 is in bits [69:48] */
        Csd->V2.CSize = ((RawResponse[1] >> 8) & 0x3FFFFF);
    }
}

/**
 * @brief Read data from the SDHCI buffer data port via PIO.
 *
 * Waits for BUFFER_READ_READY, reads the specified number of bytes
 * (must be ULONG-aligned) from the data port, and then waits for
 * XFER_COMPLETE. Used for short data reads during card enumeration
 * (e.g. SCR, EXT_CSD).
 *
 * @param[in]  FdoExtension  Pointer to the FDO device extension.
 * @param[out] Buffer        Pointer to the output buffer (must be ULONG-aligned).
 * @param[in]  Length         Number of bytes to read (must be a multiple of 4).
 *
 * @return STATUS_SUCCESS, STATUS_IO_TIMEOUT, STATUS_SD_DATA_TIMEOUT, or
 *         STATUS_SD_DATA_CRC_ERROR.
 */
static NTSTATUS
SdBusReadDataPio(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PULONG Dest = (PULONG)Buffer;
    ULONG Words = Length / sizeof(ULONG);
    ULONG DataBits;
    NTSTATUS WaitStatus;
    ULONG i;

    /* Wait for BUFFER_READ_READY via interrupt */
    WaitStatus = SdBusWaitForInterrupt(
        FdoExtension,
        SDHCI_INT_BUFFER_READ_READY,
        SDHCI_INT_DATA_ERROR_MASK,
        SD_DATA_TIMEOUT_MS,
        &DataBits);

    if (WaitStatus == STATUS_IO_DEVICE_ERROR)
    {
        (void)SdBusResetHost(FdoExtension, SDHCI_RESET_DATA);
        if (DataBits & SDHCI_INT_DATA_TIMEOUT)
        {
            return STATUS_SD_DATA_TIMEOUT;
        }
        return STATUS_SD_DATA_CRC_ERROR;
    }

    if (WaitStatus == STATUS_IO_TIMEOUT)
    {
        DPRINT1("SdBusReadDataPio: Timed out waiting for BUFFER_READ_READY\n");
        (void)SdBusResetHost(FdoExtension, SDHCI_RESET_DATA);
        return STATUS_IO_TIMEOUT;
    }

    /* Read data from the buffer data port */
    for (i = 0; i < Words; i++)
    {
        Dest[i] = SdBusReadReg32(FdoExtension, SDHCI_BUFFER_DATA_PORT);
    }

    /* Wait for transfer complete via interrupt */
    WaitStatus = SdBusWaitForInterrupt(
        FdoExtension,
        SDHCI_INT_XFER_COMPLETE,
        SDHCI_INT_DATA_ERROR_MASK,
        SD_DATA_TIMEOUT_MS,
        &DataBits);

    if (WaitStatus == STATUS_IO_DEVICE_ERROR)
    {
        (void)SdBusResetHost(FdoExtension, SDHCI_RESET_DATA);
        return STATUS_SD_DATA_CRC_ERROR;
    }

    if (WaitStatus == STATUS_IO_TIMEOUT)
    {
        DPRINT1("SdBusReadDataPio: Timed out waiting for XFER_COMPLETE\n");
        (void)SdBusResetHost(FdoExtension, SDHCI_RESET_DATA);
        return STATUS_IO_TIMEOUT;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Issue a data-bearing read command via PIO during card enumeration.
 *
 * Waits for CMD/DATA inhibit to clear, programs block size/count and
 * transfer mode for a single-block read, issues the command, waits for
 * command completion, and reads the data payload via SdBusReadDataPio.
 *
 * @param[in]  FdoExtension   Pointer to the FDO device extension.
 * @param[in]  CommandIndex    SD command index to send.
 * @param[in]  Argument        32-bit command argument.
 * @param[in]  CommandFlags    SDHCI command register flags (response type, etc.).
 * @param[out] DataBuffer      Pointer to the output buffer for the data payload.
 * @param[in]  DataLength      Number of bytes to read.
 * @param[out] Response        Optional pointer to receive the R1 response.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
static NTSTATUS
SdBusSendDataReadCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_writes_bytes_(DataLength) PVOID DataBuffer,
    _In_ ULONG DataLength,
    _Out_opt_ PULONG Response)
{
    NTSTATUS Status;
    ULONG CmdBits;
    NTSTATUS WaitStatus;
    ULONG Timeout;

    /* Wait for DATA_INHIBIT to clear (hardware readiness) */
    Timeout = SD_CMD_TIMEOUT_MS * 100;
    while (Timeout > 0)
    {
        if (!(SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE) &
              (SDHCI_PS_CMD_INHIBIT | SDHCI_PS_DATA_INHIBIT)))
        {
            break;
        }
        KeStallExecutionProcessor(10);
        Timeout--;
    }

    if (Timeout == 0)
    {
        return STATUS_IO_TIMEOUT;
    }

    /* Prepare for interrupt-driven completion */
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    KeClearEvent(&FdoExtension->CommandEvent);
    SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);

    /* Set block size and count */
    SdBusWriteReg16(FdoExtension, SDHCI_BLOCK_SIZE,
                    (USHORT)(DataLength & SDHCI_BLOCK_SIZE_MASK));
    SdBusWriteReg16(FdoExtension, SDHCI_BLOCK_COUNT, 1);

    /* Set transfer mode: single block read */
    SdBusWriteReg16(FdoExtension, SDHCI_TRANSFER_MODE,
                    SDHCI_TRNS_DATA_DIR_READ);

    /* Write argument */
    SdBusWriteReg32(FdoExtension, SDHCI_ARGUMENT, Argument);

    /* Write command register (with data present flag) */
    SdBusWriteReg16(FdoExtension, SDHCI_COMMAND,
                    SDHCI_MAKE_CMD(CommandIndex,
                                   CommandFlags | SDHCI_CMD_DATA_PRESENT));

    /* Wait for command complete via interrupt */
    WaitStatus = SdBusWaitForInterrupt(
        FdoExtension,
        SDHCI_INT_CMD_COMPLETE,
        SDHCI_INT_CMD_ERROR_MASK,
        SD_CMD_TIMEOUT_MS,
        &CmdBits);

    if (WaitStatus == STATUS_IO_DEVICE_ERROR)
    {
        (void)SdBusResetHost(FdoExtension, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        if (CmdBits & SDHCI_INT_CMD_TIMEOUT)
        {
            return STATUS_SD_CMD_TIMEOUT;
        }
        return STATUS_SD_CMD_CRC_ERROR;
    }

    if (WaitStatus == STATUS_IO_TIMEOUT)
    {
        (void)SdBusResetHost(FdoExtension, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        return STATUS_IO_TIMEOUT;
    }

    if (Response != NULL)
    {
        Response[0] = SdBusReadReg32(FdoExtension, SDHCI_RESPONSE0);
    }

    /* Now read the data via PIO */
    Status = SdBusReadDataPio(FdoExtension, DataBuffer, DataLength);
    return Status;
}

static NTSTATUS
SdBusEnableSdHighSpeed(
    _In_ PFDO_EXTENSION FdoExtension)
{
    ULONG SwitchStatusWords[16];
    PUCHAR SwitchStatus;
    NTSTATUS Status;
    UCHAR HostCtrl;
    BOOLEAN HsSupported;

    if ((FdoExtension->HostCapabilities & SDHCI_CAP_HIGH_SPEED) == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    SwitchStatus = (PUCHAR)SwitchStatusWords;
    RtlZeroMemory(SwitchStatusWords, sizeof(SwitchStatusWords));

    Status = SdBusSendDataReadCommand(FdoExtension,
                                      SDCMD_SWITCH_FUNC,
                                      0x00FFFFF1,
                                      SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                      SwitchStatus,
                                      sizeof(SwitchStatusWords),
                                      NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEnableSdHighSpeed: CMD6 check failed (0x%08lx)\n", Status);
        return Status;
    }

    HsSupported = (SwitchStatus[13] & 0x02) ? TRUE : FALSE;
    DPRINT1("SdBusEnableSdHighSpeed: CMD6 check HS=%u\n", HsSupported);
    if (!HsSupported)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(SwitchStatusWords, sizeof(SwitchStatusWords));
    Status = SdBusSendDataReadCommand(FdoExtension,
                                      SDCMD_SWITCH_FUNC,
                                      0x80FFFFF1,
                                      SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                      SwitchStatus,
                                      sizeof(SwitchStatusWords),
                                      NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEnableSdHighSpeed: CMD6 switch failed (0x%08lx)\n", Status);
        return Status;
    }

    if ((SwitchStatus[16] & 0x0F) != 1)
    {
        DPRINT1("SdBusEnableSdHighSpeed: card selected function %u\n",
                SwitchStatus[16] & 0x0F);
        return STATUS_NOT_SUPPORTED;
    }

    HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
    HostCtrl |= SDHCI_HC_HIGH_SPEED;
    SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);

    DPRINT1("SdBusEnableSdHighSpeed: SD high-speed mode enabled\n");
    return STATUS_SUCCESS;
}

static ULONG
SdBusEmmcHostWidthFromExtCsd(
    _In_ UCHAR BusWidth)
{
    switch (BusWidth)
    {
        case EMMC_BUS_WIDTH_8:
            return 8;

        case EMMC_BUS_WIDTH_4:
            return 4;

        default:
            return 1;
    }
}

static VOID
SdBusSetEmmcHostBusWidth(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR BusWidth)
{
    UCHAR HostCtrl;

    HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
    HostCtrl &= ~(SDHCI_HC_DATA_WIDTH_4BIT | SDHCI_HC_DATA_WIDTH_8BIT);

    if (BusWidth == EMMC_BUS_WIDTH_8)
    {
        HostCtrl |= SDHCI_HC_DATA_WIDTH_8BIT;
    }
    else if (BusWidth == EMMC_BUS_WIDTH_4)
    {
        HostCtrl |= SDHCI_HC_DATA_WIDTH_4BIT;
    }

    SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
    FdoExtension->CurrentBusWidth = (UCHAR)SdBusEmmcHostWidthFromExtCsd(BusWidth);
}

static NTSTATUS
SdBusReadExtCsd(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_writes_bytes_(512) PUCHAR ExtCsd)
{
    return SdBusSendDataReadCommand(FdoExtension,
                                    SDCMD_SEND_IF_COND,
                                    0,
                                    SDHCI_CMD_RESP_48 |
                                        SDHCI_CMD_CRC_CHECK |
                                        SDHCI_CMD_INDEX_CHECK,
                                    ExtCsd,
                                    512,
                                    NULL);
}

static NTSTATUS
SdBusSetEmmcBusWidth(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ ULONG Rca)
{
    static const UCHAR BusWidths[] =
    {
        EMMC_BUS_WIDTH_8,
        EMMC_BUS_WIDTH_4,
        EMMC_BUS_WIDTH_1
    };
    ULONG Index;
    ULONG FirstIndex;
    NTSTATUS LastStatus;

    LastStatus = STATUS_UNSUCCESSFUL;
    FirstIndex = (FdoExtension->HostCapabilities & SDHCI_CAP_8BIT_SUPPORT) ? 0 : 1;

    for (Index = FirstIndex; Index < RTL_NUMBER_OF(BusWidths); Index++)
    {
        ULONG VerifyExtCsdWords[128];
        PUCHAR VerifyExtCsd = (PUCHAR)VerifyExtCsdWords;
        UCHAR BusWidth = BusWidths[Index];
        NTSTATUS Status;

        Status = SdBusEmmcSwitchByRca(FdoExtension,
                                      Rca,
                                      EMMC_SWITCH_ACCESS_WRITE_BYTE,
                                      (UCHAR)EMMC_EXT_CSD_BUS_WIDTH,
                                      BusWidth,
                                      0,
                                      SD_DATA_TIMEOUT_MS);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusSetEmmcBusWidth: CMD6 width %lu-bit failed (0x%08lx)\n",
                    SdBusEmmcHostWidthFromExtCsd(BusWidth), Status);
            LastStatus = Status;
            continue;
        }

        SdBusSetEmmcHostBusWidth(FdoExtension, BusWidth);
        RtlZeroMemory(VerifyExtCsdWords, sizeof(VerifyExtCsdWords));

        Status = SdBusReadExtCsd(FdoExtension, VerifyExtCsd);
        if (NT_SUCCESS(Status) &&
            VerifyExtCsd[EMMC_EXT_CSD_BUS_WIDTH] == BusWidth)
        {
            RtlCopyMemory(PdoExtension->ExtCsd,
                          VerifyExtCsd,
                          sizeof(PdoExtension->ExtCsd));
            DPRINT1("SdBusSetEmmcBusWidth: eMMC bus width set to %lu-bit\n",
                    SdBusEmmcHostWidthFromExtCsd(BusWidth));
            return STATUS_SUCCESS;
        }

        DPRINT1("SdBusSetEmmcBusWidth: width %lu-bit verify failed "
                "(status 0x%08lx, ext_csd width 0x%02x)\n",
                SdBusEmmcHostWidthFromExtCsd(BusWidth),
                Status,
                NT_SUCCESS(Status) ? VerifyExtCsd[EMMC_EXT_CSD_BUS_WIDTH] : 0xff);
        LastStatus = NT_SUCCESS(Status) ? STATUS_DEVICE_PROTOCOL_ERROR : Status;
    }

    return LastStatus;
}

static BOOLEAN
SdBusHostSupportsUhs(
    _In_ PFDO_EXTENSION FdoExtension)
{
    if (!(FdoExtension->HostCapabilities & SDHCI_CAP_VOLTAGE_180))
    {
        return FALSE;
    }

    return (FdoExtension->HostCapabilities2 &
            (SDHCI_CAP2_SDR50_SUPPORT |
             SDHCI_CAP2_SDR104_SUPPORT |
             SDHCI_CAP2_DDR50_SUPPORT)) != 0;
}

static NTSTATUS
SdBusPerformVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension)
{
    NTSTATUS Status;
    ULONG Response = 0;
    USHORT ClockCtrl;
    USHORT HostCtrl2;
    ULONG PresentState;
    ULONG DatMask;
    ULONG Timeout;
    LARGE_INTEGER Delay;

    DatMask = SDHCI_PS_DAT0_LEVEL | SDHCI_PS_DAT1_LEVEL |
              SDHCI_PS_DAT2_LEVEL | SDHCI_PS_DAT3_LEVEL;

    Status = SdBusSendCommand(FdoExtension,
                              SDCMD_VOLTAGE_SWITCH,
                              0,
                              SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                                  SDHCI_CMD_INDEX_CHECK,
                              &Response);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusPerformVoltageSwitch: CMD11 failed (0x%08lx)\n", Status);
        return Status;
    }

    if (Response & SD_STATUS_ERROR_MASK)
    {
        DPRINT1("SdBusPerformVoltageSwitch: CMD11 response error 0x%08lx\n",
                Response);
        return STATUS_SD_VOLTAGE_SWITCH_FAILED;
    }

    Delay.QuadPart = -10000LL * SD_VOLTAGE_SWITCH_DAT_POLL_MS;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    ClockCtrl = SdBusReadReg16(FdoExtension, SDHCI_CLOCK_CONTROL);
    ClockCtrl &= ~SDHCI_CLK_SD_CLK_ENABLE;
    SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockCtrl);

    for (Timeout = 200; Timeout > 0; Timeout--)
    {
        PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);
        if (!(PresentState & DatMask))
        {
            break;
        }
        KeStallExecutionProcessor(10);
    }

    if (PresentState & DatMask)
    {
        DPRINT1("SdBusPerformVoltageSwitch: DAT[3:0] did not go low (PS=0x%08lx)\n",
                PresentState);
        ClockCtrl |= SDHCI_CLK_SD_CLK_ENABLE;
        SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockCtrl);
        return STATUS_SD_VOLTAGE_SWITCH_FAILED;
    }

    HostCtrl2 = SdBusReadReg16(FdoExtension, SDHCI_HOST_CONTROL2);
    HostCtrl2 |= SDHCI_HC2_V18_SIGNAL_ENABLE;
    SdBusWriteReg16(FdoExtension, SDHCI_HOST_CONTROL2, HostCtrl2);

    Delay.QuadPart = -10000LL * SD_VOLTAGE_SWITCH_REGULATOR_MS;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    HostCtrl2 = SdBusReadReg16(FdoExtension, SDHCI_HOST_CONTROL2);
    if (!(HostCtrl2 & SDHCI_HC2_V18_SIGNAL_ENABLE))
    {
        DPRINT1("SdBusPerformVoltageSwitch: regulator refused 1.8V (HC2=0x%04x)\n",
                HostCtrl2);
        return STATUS_SD_VOLTAGE_SWITCH_FAILED;
    }

    ClockCtrl = SdBusReadReg16(FdoExtension, SDHCI_CLOCK_CONTROL);
    ClockCtrl |= SDHCI_CLK_SD_CLK_ENABLE;
    SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockCtrl);

    Delay.QuadPart = -10000LL * SD_VOLTAGE_SWITCH_CLOCK_SETTLE_MS;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    for (Timeout = 200; Timeout > 0; Timeout--)
    {
        PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);
        if ((PresentState & DatMask) == DatMask)
        {
            DPRINT1("SdBusPerformVoltageSwitch: switch to 1.8V acknowledged "
                    "(PS=0x%08lx)\n", PresentState);
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(10);
    }

    DPRINT1("SdBusPerformVoltageSwitch: card did not acknowledge 1.8V "
            "(PS=0x%08lx)\n", PresentState);

    HostCtrl2 = SdBusReadReg16(FdoExtension, SDHCI_HOST_CONTROL2);
    HostCtrl2 &= ~SDHCI_HC2_V18_SIGNAL_ENABLE;
    SdBusWriteReg16(FdoExtension, SDHCI_HOST_CONTROL2, HostCtrl2);
    return STATUS_SD_VOLTAGE_SWITCH_FAILED;
}

/**
 * @brief Perform the full SD/MMC/eMMC card initialization and enumeration sequence.
 *
 * Executes the standard card identification flow: CMD0 (reset), CMD8
 * (voltage check), CMD5/ACMD41/CMD1 (OCR negotiation for SDIO/SD/MMC),
 * CMD2 (read CID when present), CMD3 (obtain/set RCA), CMD9 (read CSD
 * when present), CMD7 (select card), and card-type-specific
 * configuration (block length, SCR, bus width, high-speed mode).
 * Computes total sector count from CSD/EXT_CSD for storage-capable
 * media and leaves SDIO-only cards at zero sectors.
 *
 * @param[in]  FdoExtension   Pointer to the FDO device extension with initialized controller.
 * @param[out] PdoExtension   Pointer to the PDO extension to populate with card info
 *                             (card type, CID, CSD, RCA, SCR, EXT_CSD, sector count).
 *
 * @return STATUS_SUCCESS on success, STATUS_IO_TIMEOUT, STATUS_SD_CARD_NOT_DETECTED,
 *         or another NTSTATUS error code.
 */
NTSTATUS
SdBusEnumerateCard(
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PPDO_EXTENSION PdoExtension)
{
    NTSTATUS Status;
    ULONG Response[4];
    ULONG Ocr = 0;
    ULONG Rca = 0;
    ULONG SdioOcr = 0;
    ULONG NumSdioFunctions = 0;
    BOOLEAN IsV2 = FALSE;
    BOOLEAN IsHighCapacity = FALSE;
    BOOLEAN HasSdio = FALSE;
    BOOLEAN HasMemory = FALSE;
    BOOLEAN IsEmbeddedSlot;
    BOOLEAN RequestedUhs = FALSE;
    BOOLEAN CardAcceptedUhs = FALSE;
    SD_CARD_TYPE CardType = SdCardTypeUnknown;
    ULONG Timeout;
    UCHAR ScrData[8];
    LARGE_INTEGER Delay;

    RtlZeroMemory(Response, sizeof(Response));
    Delay.QuadPart = -10000LL;
    IsEmbeddedSlot =
        ((FdoExtension->HostCapabilities & SDHCI_CAP_SLOT_TYPE_MASK) ==
         SDHCI_CAP_SLOT_TYPE_EMBEDDED);

    /*
     * Step 1: CMD0 -- GO_IDLE_STATE
     * Reset all cards on the bus to idle state.
     */
    Status = SdBusSendCommand(FdoExtension,
                              SDCMD_GO_IDLE_STATE,
                              0,
                              SDHCI_CMD_RESP_NONE,
                              NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEnumerateCard: CMD0 failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Small delay after reset */
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    /*
     * Step 2: CMD8 -- SEND_IF_COND
     * Check for SD v2.0+ support. This distinguishes SD v1 from v2+.
     * The argument is 0x000001AA: VHS=1 (2.7-3.6V), check pattern=0xAA.
     */
    Status = SdBusSendCommand(FdoExtension,
                              SDCMD_SEND_IF_COND,
                              SD_CMD8_DEFAULT_ARG,
                              SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                              &Response[0]);

    if (NT_SUCCESS(Status))
    {
        /* Verify the check pattern was echoed back */
        if ((Response[0] & 0xFF) == SD_CMD8_CHECK_PATTERN)
        {
            IsV2 = TRUE;
            DPRINT1("SdBusEnumerateCard: SD v2.0+ card detected\n");
        }
        else
        {
            DPRINT1("SdBusEnumerateCard: CMD8 echo mismatch (0x%08lx)\n", Response[0]);
        }
    }
    else
    {
        /* CMD8 timeout is expected for SD v1.x, MMC, and SDIO cards */
        DPRINT1("SdBusEnumerateCard: CMD8 not supported (SD v1.x, MMC, or SDIO)\n");
    }

    /*
     * Step 2b: CMD5 -- IO_SEND_OP_COND (probe SDIO before falling back to MMC)
     *
     * SDIO and combo cards respond to CMD5 with an R4 OCR. Pure SD memory
     * and MMC media do not support this command.
     */
    Status = SdBusSendCommand(FdoExtension,
                              SDCMD_IO_SEND_OP_COND,
                              0,
                              SDHCI_CMD_RESP_48,
                              &Response[0]);
    if (NT_SUCCESS(Status))
    {
        HasSdio = TRUE;
        HasMemory = (Response[0] & SDIO_OCR_MEMORY_PRESENT) ? TRUE : FALSE;
        DPRINT1("SdBusEnumerateCard: CMD5 detected SDIO funcs=%lu memory=%u OCR=0x%08lx\n",
                (Response[0] & SDIO_OCR_NUM_FUNCTIONS_MASK) >> SDIO_OCR_NUM_FUNCTIONS_SHIFT,
                HasMemory,
                Response[0]);

        Timeout = SD_INIT_TIMEOUT_MS;
        do
        {
            Status = SdBusSendCommand(FdoExtension,
                                      SDCMD_IO_SEND_OP_COND,
                                      SD_OCR_VDD_RANGE,
                                      SDHCI_CMD_RESP_48,
                                      &Response[0]);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SdBusEnumerateCard: CMD5 failed (0x%08lx)\n", Status);
                return Status;
            }

            SdioOcr = Response[0];
            if (SdioOcr & SDIO_OCR_READY)
            {
                break;
            }

            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            Timeout--;
        } while (Timeout > 0);

        if (Timeout == 0)
        {
            DPRINT1("SdBusEnumerateCard: CMD5 timed out (card not ready)\n");
            return STATUS_IO_TIMEOUT;
        }

        HasMemory = (SdioOcr & SDIO_OCR_MEMORY_PRESENT) ? TRUE : FALSE;
        NumSdioFunctions =
            (SdioOcr & SDIO_OCR_NUM_FUNCTIONS_MASK) >> SDIO_OCR_NUM_FUNCTIONS_SHIFT;
        if (!HasMemory)
        {
            CardType = SdCardTypeSdio;
            DPRINT1("SdBusEnumerateCard: SDIO card ready, OCR=0x%08lx funcs=%lu\n",
                    SdioOcr, NumSdioFunctions);
        }
    }
    else
    {
        DPRINT("SdBusEnumerateCard: CMD5 not supported\n");
    }

    if (CardType == SdCardTypeUnknown && IsEmbeddedSlot && !HasSdio)
    {
        Status = SdBusResetHost(FdoExtension, SDHCI_RESET_CMD);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: command-line reset before CMD1 failed (0x%08lx)\n",
                    Status);
            return Status;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        Status = SdBusTryMmcOpCond(FdoExtension, &Ocr, &IsHighCapacity);
        if (NT_SUCCESS(Status))
        {
            CardType = SdCardTypeEmmc;
            DPRINT1("SdBusEnumerateCard: eMMC card ready, OCR=0x%08lx, sector=%u\n",
                    Ocr,
                    IsHighCapacity);
        }
        else
        {
            DPRINT("SdBusEnumerateCard: embedded CMD1 probe failed (0x%08lx), trying SD path\n",
                   Status);

            (void)SdBusResetHost(FdoExtension, SDHCI_RESET_CMD);
        }
    }

    /*
     * Step 3: ACMD41 -- SD_SEND_OP_COND (try SD card first)
     * For v2+: set HCS bit to indicate host supports high-capacity.
     * For v1:  do not set HCS bit.
     *
     * First send ACMD41 with argument=0 to query OCR.
     */
    if (CardType == SdCardTypeUnknown)
    {
        Status = SdBusSendAppCommand(FdoExtension,
                                     0,
                                     SDACMD_SD_SEND_OP_COND,
                                     0,
                                     SDHCI_CMD_RESP_48,
                                     &Response[0]);

        if (NT_SUCCESS(Status))
        {
            /* This is an SD memory or combo card. Loop ACMD41 until ready. */
            ULONG AcmdArg = SD_OCR_VDD_RANGE;
            if (IsV2)
            {
                AcmdArg |= SD_ACMD41_HCS;
            }

            if (IsV2 && SdBusHostSupportsUhs(FdoExtension))
            {
                AcmdArg |= SD_ACMD41_S18R;
                RequestedUhs = TRUE;
            }

            Timeout = SD_INIT_TIMEOUT_MS;
            do
            {
                Status = SdBusSendAppCommand(FdoExtension,
                                             0,
                                             SDACMD_SD_SEND_OP_COND,
                                             AcmdArg,
                                             SDHCI_CMD_RESP_48,
                                             &Response[0]);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("SdBusEnumerateCard: ACMD41 failed (0x%08lx)\n", Status);
                    return Status;
                }

                Ocr = Response[0];
                if (Ocr & SD_OCR_BUSY)
                {
                    break;
                }

                KeDelayExecutionThread(KernelMode, FALSE, &Delay);
                Timeout--;
            } while (Timeout > 0);

            if (Timeout == 0)
            {
                DPRINT1("SdBusEnumerateCard: ACMD41 timed out (card not ready)\n");
                return STATUS_IO_TIMEOUT;
            }

            IsHighCapacity = (Ocr & SD_OCR_CCS) ? TRUE : FALSE;

            if (RequestedUhs && (Ocr & SD_OCR_S18A))
            {
                CardAcceptedUhs = TRUE;
            }

            if (HasSdio)
            {
                CardType = SdCardTypeCombo;
            }
            else if (IsV2 && IsHighCapacity)
            {
                CardType = SdCardTypeSdhc;
            }
            else if (IsV2)
            {
                CardType = SdCardTypeSdV2;
            }
            else
            {
                CardType = SdCardTypeSdV1;
            }

            DPRINT1("SdBusEnumerateCard: %s card ready, OCR=0x%08lx, HC=%u, S18A=%u\n",
                    (CardType == SdCardTypeCombo) ? "SD combo" : "SD memory",
                    Ocr,
                    IsHighCapacity,
                    CardAcceptedUhs);
        }
        else
        {
            /*
             * Step 3b: CMD1 -- SEND_OP_COND (MMC/eMMC)
             * Only fall back to MMC when CMD5 did not identify SDIO media.
             */
            if (HasSdio)
            {
                DPRINT1("SdBusEnumerateCard: ACMD41 failed after CMD5 SDIO probe (0x%08lx)\n",
                        Status);
                return Status;
            }

            DPRINT1("SdBusEnumerateCard: ACMD41 failed, trying CMD1 (MMC)\n");

            /* Send CMD0 again to reset after failed ACMD41 */
            SdBusSendCommand(FdoExtension,
                             SDCMD_GO_IDLE_STATE,
                             0,
                             SDHCI_CMD_RESP_NONE,
                             NULL);
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);

            Status = SdBusTryMmcOpCond(FdoExtension, &Ocr, &IsHighCapacity);
            if (Status == STATUS_IO_TIMEOUT)
            {
                DPRINT1("SdBusEnumerateCard: CMD1 timed out\n");
                return STATUS_IO_TIMEOUT;
            }
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SdBusEnumerateCard: CMD1 failed (0x%08lx)\n", Status);
                return STATUS_SD_CARD_NOT_DETECTED;
            }

            CardType = IsEmbeddedSlot ? SdCardTypeEmmc : SdCardTypeMmc;

            DPRINT1("SdBusEnumerateCard: MMC-compatible card ready, OCR=0x%08lx, sector=%u, embedded=%u\n",
                    Ocr,
                    IsHighCapacity,
                    IsEmbeddedSlot);
        }
    }

    if (CardAcceptedUhs && CardType != SdCardTypeSdio)
    {
        Status = SdBusPerformVoltageSwitch(FdoExtension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: voltage switch failed (0x%08lx); "
                    "continuing at 3.3V\n", Status);
            CardAcceptedUhs = FALSE;
            Status = STATUS_SUCCESS;
        }
        else
        {
            DPRINT1("SdBusEnumerateCard: 1.8V signaling active; UHS-I eligible\n");
        }
    }

    /*
     * Step 4: Read card identity/registers.
     */
    if (CardType == SdCardTypeSdio)
    {
        Status = SdBusSendCommand(FdoExtension,
                                  SDCMD_SEND_RELATIVE_ADDR,
                                  0,
                                  SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                  &Response[0]);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: SDIO CMD3 failed (0x%08lx)\n", Status);
            return Status;
        }
        Rca = (Response[0] >> 16) & 0xFFFF;

        RtlZeroMemory(&PdoExtension->Cid, sizeof(PdoExtension->Cid));
        PdoExtension->Cid.ManufacturerId =
            (UCHAR)((SdioOcr & SDIO_OCR_NUM_FUNCTIONS_MASK) >> SDIO_OCR_NUM_FUNCTIONS_SHIFT);
        PdoExtension->Cid.ProductSerialNumber = SdioOcr ^ ((ULONG)Rca << 16);
    }
    else
    {
        Status = SdBusSendCommand(FdoExtension,
                                  SDCMD_ALL_SEND_CID,
                                  0,
                                  SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC_CHECK,
                                  Response);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: CMD2 failed (0x%08lx)\n", Status);
            return Status;
        }

        SdBusParseCid(Response, &PdoExtension->Cid);

        DPRINT1("SdBusEnumerateCard: CID: MID=%02X OID=%04X PNM=%.5s PSN=%08lX\n",
                PdoExtension->Cid.ManufacturerId,
                PdoExtension->Cid.OemId,
                PdoExtension->Cid.ProductName,
                PdoExtension->Cid.ProductSerialNumber);

        /*
         * Step 5: CMD3 -- SEND_RELATIVE_ADDR (SD) / SET_RELATIVE_ADDR (MMC)
         * For SD: card publishes its own RCA.
         * For MMC: we assign RCA = 1.
         */
        if (CardType == SdCardTypeMmc || CardType == SdCardTypeEmmc)
        {
            Rca = 1;
            Status = SdBusSendCommand(FdoExtension,
                                      SDCMD_SEND_RELATIVE_ADDR,
                                      Rca << 16,
                                      SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                      &Response[0]);
        }
        else
        {
            Status = SdBusSendCommand(FdoExtension,
                                      SDCMD_SEND_RELATIVE_ADDR,
                                      0,
                                      SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                      &Response[0]);
            if (NT_SUCCESS(Status))
            {
                Rca = (Response[0] >> 16) & 0xFFFF;
            }
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: CMD3 failed (0x%08lx)\n", Status);
            return Status;
        }

        /*
         * Step 6: CMD9 -- SEND_CSD
         * Read the Card-Specific Data register.
         */
        Status = SdBusSendCommand(FdoExtension,
                                  SDCMD_SEND_CSD,
                                  Rca << 16,
                                  SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC_CHECK,
                                  Response);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: CMD9 failed (0x%08lx)\n", Status);
            return Status;
        }

        SdBusParseCsd(Response, &PdoExtension->Csd);
    }

    PdoExtension->RelativeAddress = Rca;
    DPRINT1("SdBusEnumerateCard: RCA = 0x%04lX\n", Rca);

    /*
     * Step 7: CMD7 -- SELECT_CARD
     * Select the card to move it to Transfer state.
     */
    Status = SdBusSendCommand(FdoExtension,
                              SDCMD_SELECT_CARD,
                              Rca << 16,
                              SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                              &Response[0]);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusEnumerateCard: CMD7 failed (0x%08lx)\n", Status);
        return Status;
    }

    if (HasSdio && NumSdioFunctions > 0)
    {
        PdoExtension->SdioNumFunctions = (UCHAR)NumSdioFunctions;

        Status = SdBusSdioEnumerateFunctions(FdoExtension, PdoExtension,
                                             NumSdioFunctions);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: SDIO function enumeration failed "
                    "(0x%08lx), continuing with host PDO only\n", Status);
        }
        Status = STATUS_SUCCESS;
    }

    /*
     * Step 8: Card-type-specific configuration
     */
    if (CardType == SdCardTypeSdV1 || CardType == SdCardTypeSdV2 ||
        CardType == SdCardTypeSdhc || CardType == SdCardTypeSdxc ||
        CardType == SdCardTypeCombo)
    {
        /*
         * SD card: set block length, read SCR, enable 4-bit bus
         */

        /* CMD16: SET_BLOCKLEN to 512 */
        Status = SdBusSendCommand(FdoExtension,
                                  SDCMD_SET_BLOCKLEN,
                                  SD_DEFAULT_BLOCK_SIZE,
                                  SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                  NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: CMD16 failed (0x%08lx)\n", Status);
            /* Non-fatal, continue */
        }

        /*
         * ACMD51: SEND_SCR -- read the SD Configuration Register (8 bytes)
         * Need to issue CMD55 + CMD51 as a data-bearing command.
         */
        {
            ULONG AppCmdResp;

            /* CMD55 */
            Status = SdBusSendCommand(FdoExtension,
                                      SDCMD_APP_CMD,
                                      Rca << 16,
                                      SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                      &AppCmdResp);
            if (NT_SUCCESS(Status))
            {
                RtlZeroMemory(ScrData, sizeof(ScrData));
                Status = SdBusSendDataReadCommand(FdoExtension,
                                                  SDACMD_SEND_SCR,
                                                  0,
                                                  SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                                  ScrData,
                                                  8,
                                                  NULL);
                if (NT_SUCCESS(Status))
                {
                    RtlCopyMemory(PdoExtension->Scr, ScrData, 8);
                    DPRINT1("SdBusEnumerateCard: SCR read successfully\n");
                }
                else
                {
                    DPRINT1("SdBusEnumerateCard: ACMD51 data read failed (0x%08lx)\n", Status);
                }
            }
        }

        /*
         * ACMD6: SET_BUS_WIDTH -- switch to 4-bit bus if the card supports it.
         * Check the SCR register's SD_BUS_WIDTHS field (bit 2 = 4-bit support).
         * All SDHCI host controllers support 4-bit mode, so only check the card.
         */
        if ((PdoExtension->Scr[1] & SD_SCR_BUS_WIDTH_4) &&
            CardType != SdCardTypeCombo)
        {
            Status = SdBusSendAppCommand(FdoExtension,
                                         Rca,
                                         SDACMD_SET_BUS_WIDTH,
                                         SD_ACMD6_BUS_WIDTH_4,
                                         SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                         NULL);
            if (NT_SUCCESS(Status))
            {
                UCHAR HostCtrl;
                HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
                HostCtrl |= SDHCI_HC_DATA_WIDTH_4BIT;
                SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
                FdoExtension->CurrentBusWidth = 4;
                DPRINT1("SdBusEnumerateCard: Switched to 4-bit bus\n");
            }
            else
            {
                DPRINT1("SdBusEnumerateCard: ACMD6 failed, staying on 1-bit bus\n");
            }
        }
        else if ((PdoExtension->Scr[1] & SD_SCR_BUS_WIDTH_4) &&
                 CardType == SdCardTypeCombo)
        {
            DPRINT1("SdBusEnumerateCard: Leaving combo card on 1-bit bus until SDIO bus-width switching is coordinated\n");
        }

        Status = SdBusEnableSdHighSpeed(FdoExtension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: SD high-speed switch not active (0x%08lx)\n",
                    Status);
            Status = STATUS_SUCCESS;
        }
    }
    else if (CardType == SdCardTypeMmc || CardType == SdCardTypeEmmc)
    {
        /*
         * MMC/eMMC: read EXT_CSD, then configure bus width and speed.
         */

        /* CMD16: SET_BLOCKLEN to 512 */
        SdBusSendCommand(FdoExtension,
                         SDCMD_SET_BLOCKLEN,
                         SD_DEFAULT_BLOCK_SIZE,
                         SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                         NULL);

        /*
         * CMD8 (for MMC): SEND_EXT_CSD -- 512-byte data read
         */
        RtlZeroMemory(PdoExtension->ExtCsd, sizeof(PdoExtension->ExtCsd));
        Status = SdBusSendDataReadCommand(FdoExtension,
                                          SDCMD_SEND_IF_COND, /* CMD8 = SEND_EXT_CSD for MMC */
                                          0,
                                          SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                                          PdoExtension->ExtCsd,
                                          512,
                                          NULL);
        if (NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: EXT_CSD read successfully\n");
            FdoExtension->EmmcPartitionConfig =
                PdoExtension->ExtCsd[EMMC_EXT_CSD_PARTITION_CONFIG];
            FdoExtension->EmmcPartitionConfigValid = TRUE;
        }
        else
        {
            DPRINT1("SdBusEnumerateCard: CMD8 (EXT_CSD) failed (0x%08lx)\n", Status);
        }

        /*
         * CMD6 (SWITCH): set bus width in EXT_CSD[183]
         */
        Status = SdBusSetEmmcBusWidth(FdoExtension, PdoExtension, Rca);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusEnumerateCard: eMMC bus width selection failed (0x%08lx)\n",
                    Status);
            return Status;
        }

        /*
         * CMD6 (SWITCH): enable high-speed mode via EXT_CSD[185].
         */
        {
            UCHAR DeviceType = PdoExtension->ExtCsd[EMMC_EXT_CSD_DEVICE_TYPE];

            if (FdoExtension->CurrentBusWidth == 1)
            {
                DPRINT1("SdBusEnumerateCard: keeping eMMC default timing on 1-bit bus\n");
            }
            else if ((FdoExtension->HostCapabilities & SDHCI_CAP_HIGH_SPEED) &&
                     (DeviceType & EMMC_DEVICE_TYPE_HS_52))
            {
                /*
                 * CMD6 is R1b. After DAT0 busy release, switch the host timing
                 * before sending any further status commands.
                 */
                Status = SdBusEmmcSwitchByRca(FdoExtension,
                                              Rca,
                                              EMMC_SWITCH_ACCESS_WRITE_BYTE,
                                              (UCHAR)EMMC_EXT_CSD_HS_TIMING,
                                              EMMC_TIMING_HIGH_SPEED,
                                              0,
                                              0);
                if (NT_SUCCESS(Status))
                {
                    UCHAR HostCtrl;

                    HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
                    HostCtrl |= SDHCI_HC_HIGH_SPEED;
                    SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
                    PdoExtension->ExtCsd[EMMC_EXT_CSD_HS_TIMING] =
                        EMMC_TIMING_HIGH_SPEED;
                    DPRINT1("SdBusEnumerateCard: eMMC high-speed mode enabled\n");
                }
                else
                {
                    DPRINT("SdBusEnumerateCard: eMMC high-speed switch failed (0x%08lx)\n",
                           Status);
                }
            }
        }

        PdoExtension->CardType = CardType;
        SdBusEmmcEnumeratePartitions(FdoExtension, PdoExtension);
    }

    /* Store the determined card type */
    PdoExtension->CardType = CardType;
    PdoExtension->HighCapacity = IsHighCapacity;
    switch (CardType)
    {
        case SdCardTypeSdV1:
        case SdCardTypeSdV2:
        case SdCardTypeSdhc:
        case SdCardTypeSdxc:
        case SdCardTypeCombo:
        {
            ULONG PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);

            /*
             * SDHCI reports the mechanical write-protect pin as 1 when writes
             * are allowed, so cache the inverted sense exposed to class drivers.
             */
            PdoExtension->WriteProtected =
                (PresentState & SDHCI_PS_WRITE_PROTECT) ? FALSE : TRUE;
            break;
        }

        default:
            PdoExtension->WriteProtected = FALSE;
            break;
    }

    PdoExtension->BytesPerSector = SD_DEFAULT_BLOCK_SIZE;

    if (CardType == SdCardTypeSdio)
    {
        PdoExtension->TotalSectors = 0;
    }
    else if (CardType == SdCardTypeEmmc ||
             (CardType == SdCardTypeMmc && IsHighCapacity))
    {
        /* Sector-addressed MMC media expose capacity via EXT_CSD[212:215]. */
        PdoExtension->TotalSectors = (ULONGLONG)EMMC_SECTOR_COUNT(PdoExtension->ExtCsd);
    }
    else if (PdoExtension->Csd.CsdVersion == 0)
    {
        /* CSD v1 (SDSC): capacity = (C_SIZE+1) * 2^(C_SIZE_MULT+2) * 2^READ_BL_LEN */
        PdoExtension->TotalSectors =
            SD_CSD_V1_CAPACITY(&PdoExtension->Csd.V1) / SD_DEFAULT_BLOCK_SIZE;
    }
    else
    {
        /* CSD v2 (SDHC/SDXC): sectors = (C_SIZE+1) * 1024 */
        PdoExtension->TotalSectors =
            SD_CSD_V2_SECTORS(&PdoExtension->Csd.V2);
    }

    DPRINT1("SdBusEnumerateCard: Enumeration complete, CardType=%d, HighCapacity=%u, TotalSectors=%I64u\n",
            CardType, IsHighCapacity, PdoExtension->TotalSectors);

    return STATUS_SUCCESS;
}

/**
 * @brief Raise the SDHCI clock from 400 kHz init speed to transfer speed.
 *
 * Called after card enumeration completes successfully. Selects 25 MHz for
 * SD cards (Default Speed) or 52 MHz for eMMC in high-speed timing mode.
 * Non-fatal: the card works at 400 kHz if this fails.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 * @param[in] PdoExtension  Pointer to the child PDO extension (for card type).
 *
 * @return STATUS_SUCCESS or STATUS_IO_TIMEOUT.
 */
NTSTATUS
SdBusSetTransferClock(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension)
{
    ULONG TargetClockKhz;
    USHORT Divisor;
    USHORT DivisorHigh;
    USHORT ClockControl;
    ULONG Timeout;

    /* Pick target clock based on card type and host speed mode */
    if (PdoExtension->CardType == SdCardTypeEmmc ||
        PdoExtension->CardType == SdCardTypeMmc)
    {
        UCHAR HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
        if ((HostCtrl & SDHCI_HC_HIGH_SPEED) &&
            PdoExtension->ExtCsd[EMMC_EXT_CSD_HS_TIMING] == EMMC_TIMING_HIGH_SPEED)
        {
            TargetClockKhz = MMC_HIGH_SPEED_KHZ;   /* 52 MHz */
        }
        else
        {
            TargetClockKhz = SD_DEFAULT_SPEED_KHZ;  /* 25 MHz */
        }
    }
    else
    {
        UCHAR HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
        if (HostCtrl & SDHCI_HC_HIGH_SPEED)
            TargetClockKhz = SD_HIGH_SPEED_KHZ;     /* 50 MHz */
        else
            TargetClockKhz = SD_DEFAULT_SPEED_KHZ;  /* 25 MHz */
    }

    /* Clamp to base clock */
    if (FdoExtension->MaxClockFrequency > 0 &&
        TargetClockKhz > FdoExtension->MaxClockFrequency)
    {
        TargetClockKhz = FdoExtension->MaxClockFrequency;
    }

    DPRINT1("SdBusSetTransferClock: Setting transfer clock to %lu kHz\n",
            TargetClockKhz);

    /* Disable SD clock output */
    ClockControl = SdBusReadReg16(FdoExtension, SDHCI_CLOCK_CONTROL);
    ClockControl &= ~SDHCI_CLK_SD_CLK_ENABLE;
    SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);

    /* Compute divisor */
    if (FdoExtension->MaxClockFrequency > 0)
    {
        Divisor = (USHORT)SDHCI_CALC_CLK_DIVIDER(FdoExtension->MaxClockFrequency,
                                              TargetClockKhz);
    }
    else
    {
        Divisor = 0;
    }
    if (Divisor > 0x3FF)
    {
        Divisor = 0x3FF;
    }

    DivisorHigh = (Divisor & 0x300) >> 2;
    ClockControl = (USHORT)((Divisor & 0xFF) << SDHCI_CLK_FREQ_SEL_SHIFT);
    ClockControl |= (USHORT)DivisorHigh;
    ClockControl |= SDHCI_CLK_INT_CLK_ENABLE;
    SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);

    /* Wait for internal clock to stabilize */
    Timeout = 200;
    while (Timeout > 0)
    {
        ClockControl = SdBusReadReg16(FdoExtension, SDHCI_CLOCK_CONTROL);
        if (ClockControl & SDHCI_CLK_INT_CLK_STABLE)
        {
            break;
        }
        KeStallExecutionProcessor(1000);
        Timeout--;
    }

    if (Timeout == 0)
    {
        DPRINT1("SdBusSetTransferClock: Clock stabilization timed out\n");
        return STATUS_IO_TIMEOUT;
    }

    /* Enable SD clock output */
    ClockControl |= SDHCI_CLK_SD_CLK_ENABLE;
    SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);

    return STATUS_SUCCESS;
}
