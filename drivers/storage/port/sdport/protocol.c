/*
 * PROJECT:     ReactOS SD Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SD/SDIO/eMMC card initialization state machines
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include "sdport.h"

#define NDEBUG
#include <debug.h>

/* PRIVATE HELPERS ************************************************************/

#define SDPORT_SDIO_OCR_READY               0x80000000
#define SDPORT_SDIO_OCR_NUM_FUNCTIONS_MASK  0x70000000
#define SDPORT_SDIO_OCR_NUM_FUNCTIONS_SHIFT 28
#define SDPORT_SDIO_OCR_MEMORY_PRESENT      0x08000000

static
NTSTATUS
SdPortVerifyEmmcSwitchStatus(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension)
{
    SDPORT_REQUEST Request;
    NTSTATUS Status;
    ULONG CardStatus;

    SdPortBuildCommand(&Request,
                       SDCMD_SEND_STATUS,
                       SlotExtension->Rca,
                       SDRT_1,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortVerifyEmmcSwitchStatus: CMD13 failed (0x%08lx)\n", Status);
        return Status;
    }

    CardStatus = Request.Response[0];
    if (CardStatus & MMC_STATUS_SWITCH_ERROR)
    {
        DPRINT1("SdPortVerifyEmmcSwitchStatus: SWITCH_ERROR set in card status 0x%08lx\n",
                CardStatus);
        return STATUS_SD_IO_ERROR;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Parse the Card Identification Register from a raw R2 response.
 *
 * Extracts CID fields from the 128-bit R2 response. The response arrives
 * as four 32-bit words in the SDPORT_REQUEST.Response[] array, packed
 * MSB-first across Response[3]..Response[0] per the SD specification.
 *
 * @param[in]  RawResponse  Array of 4 ULONGs containing the raw R2 response.
 * @param[out] Cid          Pointer to the SD_CID structure to populate.
 */
static
VOID
SdPortParseCid(
    _In_ PULONG RawResponse,
    _Out_ PSD_CID Cid)
{
    RtlZeroMemory(Cid, sizeof(SD_CID));

    /* MID [127:120] is in the upper byte of Response[3] */
    Cid->ManufacturerId = (UCHAR)(RawResponse[3] >> 24);

    /* OID [119:104] */
    Cid->OemId = (USHORT)(RawResponse[3] >> 8);

    /* PNM [103:64] - 5 ASCII characters */
    Cid->ProductName[0] = (UCHAR)(RawResponse[3] & 0xFF);
    Cid->ProductName[1] = (UCHAR)(RawResponse[2] >> 24);
    Cid->ProductName[2] = (UCHAR)(RawResponse[2] >> 16);
    Cid->ProductName[3] = (UCHAR)(RawResponse[2] >> 8);
    Cid->ProductName[4] = (UCHAR)(RawResponse[2] & 0xFF);

    /* PRV [63:56] */
    Cid->ProductRevision = (UCHAR)(RawResponse[1] >> 24);

    /* PSN [55:24] */
    Cid->ProductSerialNumber = (RawResponse[1] << 8) | (RawResponse[0] >> 24);

    /* MDT [19:8] */
    Cid->ManufacturingDate = (USHORT)((RawResponse[0] >> 8) & 0x0FFF);

    /* CRC7 [7:1] plus end bit */
    Cid->Crc7 = (UCHAR)(RawResponse[0] & 0xFF);
}

/**
 * @brief Parse a version 1 CSD from a raw R2 response.
 *
 * Decodes the Card-Specific Data register for SD Standard Capacity (SDSC)
 * cards and eMMC cards from the 128-bit R2 response.
 *
 * @param[in]  RawResponse  Array of 4 ULONGs containing the raw R2 response.
 * @param[out] Csd          Pointer to the SD_CSD structure to populate.
 */
static
VOID
SdPortParseCsdV1(
    _In_ PULONG RawResponse,
    _Out_ PSD_CSD Csd)
{
    RtlZeroMemory(Csd, sizeof(SD_CSD));
    Csd->CsdVersion = 0;
    RtlCopyMemory(Csd->Raw, RawResponse, 16);

    Csd->V1.CsdStructure = (UCHAR)(RawResponse[3] >> 30);
    Csd->V1.Taac = (UCHAR)(RawResponse[3] >> 16);
    Csd->V1.Nsac = (UCHAR)(RawResponse[3] >> 8);
    Csd->V1.TranSpeed = (UCHAR)(RawResponse[3]);
    Csd->V1.Ccc = (USHORT)(RawResponse[2] >> 20);
    Csd->V1.ReadBlLen = (UCHAR)((RawResponse[2] >> 16) & 0x0F);
    Csd->V1.ReadBlPartial = (BOOLEAN)((RawResponse[2] >> 15) & 1);
    Csd->V1.WriteBlkMisalign = (BOOLEAN)((RawResponse[2] >> 14) & 1);
    Csd->V1.ReadBlkMisalign = (BOOLEAN)((RawResponse[2] >> 13) & 1);
    Csd->V1.DsrImp = (BOOLEAN)((RawResponse[2] >> 12) & 1);
    Csd->V1.CSize = (USHORT)(((RawResponse[2] & 0x3FF) << 2) |
                              (RawResponse[1] >> 30));
    Csd->V1.VddRCurrMin = (UCHAR)((RawResponse[1] >> 27) & 0x07);
    Csd->V1.VddRCurrMax = (UCHAR)((RawResponse[1] >> 24) & 0x07);
    Csd->V1.VddWCurrMin = (UCHAR)((RawResponse[1] >> 21) & 0x07);
    Csd->V1.VddWCurrMax = (UCHAR)((RawResponse[1] >> 18) & 0x07);
    Csd->V1.CSizeMult = (UCHAR)((RawResponse[1] >> 15) & 0x07);
    Csd->V1.EraseBlkEn = (BOOLEAN)((RawResponse[1] >> 14) & 1);
    Csd->V1.SectorSize = (UCHAR)((RawResponse[1] >> 7) & 0x7F);
    Csd->V1.WpGrpSize = (UCHAR)(RawResponse[1] & 0x7F);
    Csd->V1.WpGrpEnable = (BOOLEAN)((RawResponse[0] >> 31) & 1);
    Csd->V1.R2wFactor = (UCHAR)((RawResponse[0] >> 26) & 0x07);
    Csd->V1.WriteBlLen = (UCHAR)((RawResponse[0] >> 22) & 0x0F);
    Csd->V1.WriteBlPartial = (BOOLEAN)((RawResponse[0] >> 21) & 1);
    Csd->V1.FileFormatGrp = (BOOLEAN)((RawResponse[0] >> 15) & 1);
    Csd->V1.Copy = (BOOLEAN)((RawResponse[0] >> 14) & 1);
    Csd->V1.PermWriteProtect = (BOOLEAN)((RawResponse[0] >> 13) & 1);
    Csd->V1.TmpWriteProtect = (BOOLEAN)((RawResponse[0] >> 12) & 1);
    Csd->V1.FileFormat = (UCHAR)((RawResponse[0] >> 10) & 0x03);
}

/**
 * @brief Parse a version 2 CSD from a raw R2 response.
 *
 * Decodes the Card-Specific Data register for SDHC/SDXC cards from the
 * 128-bit R2 response. The v2 CSD has a larger 22-bit C_SIZE field.
 *
 * @param[in]  RawResponse  Array of 4 ULONGs containing the raw R2 response.
 * @param[out] Csd          Pointer to the SD_CSD structure to populate.
 */
static
VOID
SdPortParseCsdV2(
    _In_ PULONG RawResponse,
    _Out_ PSD_CSD Csd)
{
    RtlZeroMemory(Csd, sizeof(SD_CSD));
    Csd->CsdVersion = 1;
    RtlCopyMemory(Csd->Raw, RawResponse, 16);

    Csd->V2.CsdStructure = (UCHAR)(RawResponse[3] >> 30);
    Csd->V2.Taac = (UCHAR)(RawResponse[3] >> 16);
    Csd->V2.Nsac = (UCHAR)(RawResponse[3] >> 8);
    Csd->V2.TranSpeed = (UCHAR)(RawResponse[3]);
    Csd->V2.Ccc = (USHORT)(RawResponse[2] >> 20);
    Csd->V2.ReadBlLen = (UCHAR)((RawResponse[2] >> 16) & 0x0F);
    Csd->V2.ReadBlPartial = (BOOLEAN)((RawResponse[2] >> 15) & 1);
    Csd->V2.WriteBlkMisalign = (BOOLEAN)((RawResponse[2] >> 14) & 1);
    Csd->V2.ReadBlkMisalign = (BOOLEAN)((RawResponse[2] >> 13) & 1);
    Csd->V2.DsrImp = (BOOLEAN)((RawResponse[2] >> 12) & 1);

    /*
     * For CSD v2, C_SIZE is a 22-bit field at [69:48].
     * Response[1] bits [21:0] contain C_SIZE[21:0].
     */
    Csd->V2.CSize = ((RawResponse[2] & 0x3F) << 16) |
                     (RawResponse[1] >> 16);

    Csd->V2.EraseBlkEn = (BOOLEAN)((RawResponse[1] >> 14) & 1);
    Csd->V2.SectorSize = (UCHAR)((RawResponse[1] >> 7) & 0x7F);
    Csd->V2.WpGrpSize = (UCHAR)(RawResponse[1] & 0x7F);
    Csd->V2.WpGrpEnable = (BOOLEAN)((RawResponse[0] >> 31) & 1);
    Csd->V2.R2wFactor = (UCHAR)((RawResponse[0] >> 26) & 0x07);
    Csd->V2.WriteBlLen = (UCHAR)((RawResponse[0] >> 22) & 0x0F);
    Csd->V2.WriteBlPartial = (BOOLEAN)((RawResponse[0] >> 21) & 1);
    Csd->V2.FileFormatGrp = (BOOLEAN)((RawResponse[0] >> 15) & 1);
    Csd->V2.Copy = (BOOLEAN)((RawResponse[0] >> 14) & 1);
    Csd->V2.PermWriteProtect = (BOOLEAN)((RawResponse[0] >> 13) & 1);
    Csd->V2.TmpWriteProtect = (BOOLEAN)((RawResponse[0] >> 12) & 1);
    Csd->V2.FileFormat = (UCHAR)((RawResponse[0] >> 10) & 0x03);
}

/**
 * @brief Parse the SD Configuration Register from raw ACMD51 data.
 *
 * Decodes the 64-bit SCR register from 8 bytes of raw data transmitted
 * MSB-first by the card in response to ACMD51.
 *
 * @param[in]  RawData  Pointer to 8 bytes of raw SCR data.
 * @param[out] Scr      Pointer to the SD_SCR structure to populate.
 */
static
VOID
SdPortParseScr(
    _In_ PUCHAR RawData,
    _Out_ PSD_SCR Scr)
{
    ULONG Word0;
    ULONG Word1;

    RtlZeroMemory(Scr, sizeof(SD_SCR));

    /*
     * SCR is transmitted MSB-first as 8 bytes.
     * Word0 covers bytes [0..3], Word1 covers bytes [4..7].
     */
    Word0 = ((ULONG)RawData[0] << 24) | ((ULONG)RawData[1] << 16) |
            ((ULONG)RawData[2] << 8)  |  (ULONG)RawData[3];
    Word1 = ((ULONG)RawData[4] << 24) | ((ULONG)RawData[5] << 16) |
            ((ULONG)RawData[6] << 8)  |  (ULONG)RawData[7];

    Scr->Raw[0] = Word0;
    Scr->Raw[1] = Word1;

    Scr->ScrStructure = (UCHAR)((Word0 >> 28) & 0x0F);
    Scr->SdSpec = (UCHAR)((Word0 >> 24) & 0x0F);
    Scr->DataStatAfterErase = (BOOLEAN)((Word0 >> 23) & 1);
    Scr->SdSecurity = (UCHAR)((Word0 >> 20) & 0x07);
    Scr->SdBusWidths = (UCHAR)((Word0 >> 16) & 0x0F);
    Scr->SdSpec3 = (BOOLEAN)((Word0 >> 15) & 1);
    Scr->ExSecurity = (UCHAR)((Word0 >> 11) & 0x0F);
    Scr->SdSpec4 = (BOOLEAN)((Word0 >> 10) & 1);
    Scr->SdSpecX = (UCHAR)((Word0 >> 6) & 0x0F);
    Scr->CmdSupport = (UCHAR)(Word0 & 0x03);
}

/**
 * @brief Delay execution for a specified number of milliseconds.
 *
 * Performs a PASSIVE_LEVEL wait using KeDelayExecutionThread.
 *
 * @param[in] Milliseconds  The number of milliseconds to wait.
 */
static
VOID
SdPortDelayMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/**
 * @brief Determine the card type and run the appropriate initialization.
 *
 * Resets the host, sets 400 kHz initial clock and 3.3V bus power, sends
 * CMD0 (GO_IDLE_STATE) and CMD8 (SEND_IF_COND) to probe for SD v2+.
 * Then tries the SD initialization sequence (ACMD41); if that fails,
 * falls back to the eMMC initialization sequence (CMD1).
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in,out] SlotExtension  Pointer to the slot extension for the card.
 *
 * @return STATUS_SUCCESS on success, STATUS_SD_UNSUPPORTED_CARD if the card
 *         could not be identified, or another NTSTATUS error code.
 */
NTSTATUS
SdPortInitializeCard(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension)
{
    SDPORT_REQUEST Request;
    NTSTATUS Status;
    BOOLEAN IsV2Card = FALSE;

    DPRINT1("SdPortInitializeCard: Slot %u\n", SlotExtension->SlotIndex);

    /* Reset the host controller */
    Status = SdPortResetHost(FdoExtension, SlotExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeCard: Host reset failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Set initial clock to 400 kHz as required by the SD specification */
    Status = SdPortSetSlotClock(FdoExtension, SlotExtension, SD_INIT_CLOCK_KHZ);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeCard: Set clock failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Power the bus at 3.3V */
    Status = SdPortSetSlotPower(FdoExtension, SlotExtension,
                                SDHCI_PC_BUS_VOLTAGE_330);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeCard: Set power failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Small delay to allow power to stabilize */
    SdPortDelayMs(10);

    /* Set 1-bit bus width initially */
    Status = SdPortSetBusWidth(FdoExtension, SlotExtension, 1);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeCard: Set bus width failed (0x%08lx)\n", Status);
        return Status;
    }

    /* CMD0: GO_IDLE_STATE - reset card to idle */
    SdPortBuildCommand(&Request,
                       SDCMD_GO_IDLE_STATE,
                       0,
                       SDRT_NONE,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeCard: CMD0 failed (0x%08lx)\n", Status);
        return Status;
    }

    SdPortDelayMs(2);

    SdPortBuildCommand(&Request,
                       SDCMD_IO_SEND_OP_COND,
                       0,
                       SDRT_4,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (NT_SUCCESS(Status))
    {
        UCHAR NumFunctions;
        BOOLEAN MemPresent;

        NumFunctions = (UCHAR)((Request.Response[0] &
                                SDPORT_SDIO_OCR_NUM_FUNCTIONS_MASK) >>
                               SDPORT_SDIO_OCR_NUM_FUNCTIONS_SHIFT);
        MemPresent = (Request.Response[0] & SDPORT_SDIO_OCR_MEMORY_PRESENT) ?
                     TRUE : FALSE;

        DPRINT1("SdPortInitializeCard: CMD5 OCR=0x%08lx NumFn=%u MemPresent=%u\n",
                Request.Response[0], NumFunctions, MemPresent);

        if (NumFunctions > 0 && !MemPresent)
        {
            SlotExtension->CardType = SdCardTypeSdio;
            SlotExtension->Initialized = TRUE;
            DPRINT1("SdPortInitializeCard: Pure SDIO card detected\n");
            return STATUS_SUCCESS;
        }

        if (NumFunctions > 0 && MemPresent)
        {
            SlotExtension->CardType = SdCardTypeCombo;
            DPRINT1("SdPortInitializeCard: SD combo card detected\n");
        }
    }
    else if (Status == STATUS_SD_CMD_TIMEOUT)
    {
        DPRINT1("SdPortInitializeCard: CMD5 timed out (not SDIO)\n");
    }
    else
    {
        DPRINT1("SdPortInitializeCard: CMD5 error (0x%08lx), treating as non-SDIO\n",
                Status);
    }

    SdPortBuildCommand(&Request,
                       SDCMD_GO_IDLE_STATE,
                       0,
                       SDRT_NONE,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeCard: CMD0 (post-CMD5) failed (0x%08lx)\n", Status);
        return Status;
    }

    SdPortDelayMs(2);

    /*
     * CMD8: SEND_IF_COND - probe for SD v2.0+ capability.
     * Only SD v2+ cards respond to this. If the card does not respond
     * (timeout), it is either an SD v1 card or an eMMC card.
     */
    SdPortBuildCommand(&Request,
                       SDCMD_SEND_IF_COND,
                       SD_CMD8_DEFAULT_ARG,
                       SDRT_1,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (NT_SUCCESS(Status))
    {
        /* Card responded to CMD8: verify the echo pattern */
        if ((Request.Response[0] & 0x1FF) == SD_CMD8_DEFAULT_ARG)
        {
            IsV2Card = TRUE;
            DPRINT1("SdPortInitializeCard: SD v2+ card detected\n");
        }
        else
        {
            DPRINT1("SdPortInitializeCard: CMD8 bad echo (0x%08lx)\n",
                    Request.Response[0]);
            return STATUS_SD_UNSUPPORTED_CARD;
        }
    }
    else if (Status == STATUS_SD_CMD_TIMEOUT)
    {
        /* No response to CMD8: could be SD v1 or eMMC */
        DPRINT1("SdPortInitializeCard: No CMD8 response, trying SD v1/eMMC\n");
        IsV2Card = FALSE;
    }
    else
    {
        DPRINT1("SdPortInitializeCard: CMD8 error (0x%08lx)\n", Status);
        return Status;
    }

    /*
     * Try the SD initialization sequence first (ACMD41).
     * If that fails with a timeout, fall back to eMMC (CMD1).
     */
    SlotExtension->CardType = SdCardTypeUnknown;

    if (IsV2Card)
    {
        /* Definitely an SD v2+ card */
        Status = SdPortInitializeSdCard(FdoExtension, SlotExtension, TRUE);
        if (NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    else
    {
        /*
         * Ambiguous: try SD first.  We send CMD0 again to ensure
         * the card is in idle state before the ACMD41 attempt.
         */
        SdPortBuildCommand(&Request,
                           SDCMD_GO_IDLE_STATE,
                           0,
                           SDRT_NONE,
                           SDTT_CMD_ONLY,
                           SDTD_UNSPECIFIED);
        SdPortSendCommand(FdoExtension, SlotExtension, &Request);
        SdPortDelayMs(2);

        Status = SdPortInitializeSdCard(FdoExtension, SlotExtension, FALSE);
        if (NT_SUCCESS(Status))
        {
            return Status;
        }

        /* SD init failed; reset and try eMMC */
        DPRINT1("SdPortInitializeCard: SD init failed, trying eMMC\n");

        SdPortBuildCommand(&Request,
                           SDCMD_GO_IDLE_STATE,
                           0,
                           SDRT_NONE,
                           SDTT_CMD_ONLY,
                           SDTD_UNSPECIFIED);
        SdPortSendCommand(FdoExtension, SlotExtension, &Request);
        SdPortDelayMs(2);

        Status = SdPortInitializeEmmcCard(FdoExtension, SlotExtension);
        if (NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    DPRINT1("SdPortInitializeCard: All init sequences failed\n");
    return STATUS_SD_UNSUPPORTED_CARD;
}

/**
 * @brief Run the full SD memory card initialization sequence.
 *
 * Performs ACMD41 loop (with HCS bit if v2) to negotiate OCR, then
 * CMD2 (ALL_SEND_CID), CMD3 (SEND_RELATIVE_ADDR), CMD9 (SEND_CSD),
 * CMD7 (SELECT_CARD), ACMD51 (SEND_SCR), ACMD6 (SET_BUS_WIDTH to 4-bit),
 * CMD16 (SET_BLOCKLEN to 512), and bus speed negotiation.
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in,out] SlotExtension  Pointer to the slot extension for the card.
 * @param[in]     IsV2Card       TRUE if the card responded to CMD8 (SD v2+).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortInitializeSdCard(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ BOOLEAN IsV2Card)
{
    SDPORT_REQUEST Request;
    ULONG Ocr;
    ULONG Argument;
    UCHAR ScrBuffer[8];
    NTSTATUS Status;
    ULONGLONG Start;
    ULONGLONG Deadline;
    BOOLEAN Ready;

    DPRINT1("SdPortInitializeSdCard: Slot %u, V2=%u\n",
           SlotExtension->SlotIndex, IsV2Card);

    /*
     * Step 1: ACMD41 loop - negotiate operating conditions.
     *
     */
    Argument = SD_OCR_VDD_RANGE;

    /*
     * Set HCS (host capacity support) only for v2+ cards that responded
     * to CMD8. SD v1.x cards do not understand HCS and must not receive it.
     */
    if (IsV2Card)
    {
        Argument |= SD_ACMD41_HCS;
    }

    Start = KeQueryInterruptTime();
    Deadline = Start + 10000000ULL;
    Ocr = 0;
    Ready = FALSE;

    while (TRUE)
    {
        SdPortBuildCommand(&Request,
                           SDACMD_SD_SEND_OP_COND,
                           Argument,
                           SDRT_3,
                           SDTT_CMD_ONLY,
                           SDTD_UNSPECIFIED);

        Status = SdPortSendAppCommand(FdoExtension, SlotExtension, &Request, 0);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdPortInitializeSdCard: ACMD41 failed (0x%08lx)\n", Status);
            return Status;
        }

        Ocr = Request.Response[0];
        if (Ocr & SD_OCR_BUSY)
        {
            Ready = TRUE;
            break;
        }

        if (KeQueryInterruptTime() >= Deadline)
        {
            break;
        }

        SdPortDelayMs(10);
    }

    if (!Ready)
    {
        DPRINT1("SdPortInitializeSdCard: ACMD41 timeout (1s deadline)\n");
        return STATUS_SD_CMD_TIMEOUT;
    }

    /* Determine card type from OCR */
    if (Ocr & SD_OCR_CCS)
    {
        SlotExtension->CardType = SdCardTypeSdhc;
        SlotExtension->HighCapacity = TRUE;
        DPRINT1("SdPortInitializeSdCard: SDHC/SDXC card\n");
    }
    else if (IsV2Card)
    {
        SlotExtension->CardType = SdCardTypeSdV2;
        SlotExtension->HighCapacity = FALSE;
        DPRINT1("SdPortInitializeSdCard: SDSC v2 card\n");
    }
    else
    {
        SlotExtension->CardType = SdCardTypeSdV1;
        SlotExtension->HighCapacity = FALSE;
        DPRINT1("SdPortInitializeSdCard: SD v1.x card\n");
    }

    /*
     * Step 2: CMD2 - ALL_SEND_CID
     * Puts the card into Identification state and reads the CID.
     */
    SdPortBuildCommand(&Request,
                       SDCMD_ALL_SEND_CID,
                       0,
                       SDRT_2,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeSdCard: CMD2 failed (0x%08lx)\n", Status);
        return Status;
    }

    SdPortParseCid(Request.Response, &SlotExtension->Cid);
    DPRINT1("SdPortInitializeSdCard: CID MID=0x%02x OID=0x%04x\n",
           SlotExtension->Cid.ManufacturerId,
           SlotExtension->Cid.OemId);

    /*
     * Step 3: CMD3 - SEND_RELATIVE_ADDR
     * The card publishes its RCA (upper 16 bits of response).
     */
    SdPortBuildCommand(&Request,
                       SDCMD_SEND_RELATIVE_ADDR,
                       0,
                       SDRT_6,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeSdCard: CMD3 failed (0x%08lx)\n", Status);
        return Status;
    }

    SlotExtension->Rca = Request.Response[0] & 0xFFFF0000;
    DPRINT1("SdPortInitializeSdCard: RCA=0x%08lx\n", SlotExtension->Rca);

    /*
     * Step 4: CMD9 - SEND_CSD
     * Read the Card-Specific Data register.
     */
    SdPortBuildCommand(&Request,
                       SDCMD_SEND_CSD,
                       SlotExtension->Rca,
                       SDRT_2,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeSdCard: CMD9 failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Determine CSD version and parse */
    if ((Request.Response[3] >> 30) == 0)
    {
        SdPortParseCsdV1(Request.Response, &SlotExtension->Csd);
    }
    else
    {
        SdPortParseCsdV2(Request.Response, &SlotExtension->Csd);
    }

    /*
     * Step 5: CMD7 - SELECT_CARD
     * Moves the card from Standby to Transfer state.
     */
    SdPortBuildCommand(&Request,
                       SDCMD_SELECT_CARD,
                       SlotExtension->Rca,
                       SDRT_1B,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeSdCard: CMD7 failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Increase clock to default speed (25 MHz) now that card is selected */
    Status = SdPortSetSlotClock(FdoExtension, SlotExtension, SD_DEFAULT_SPEED_KHZ);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeSdCard: Set 25MHz clock failed (0x%08lx)\n", Status);
        /* Non-fatal, continue at current clock */
    }

    /*
     * Step 6: ACMD51 - SEND_SCR
     * Read the SD Configuration Register (8 bytes).
     * ACMD51 is an application command — mark CmdClass so that
     * SdPortSendCommandWithData sends CMD55 before the data command.
     */
    RtlZeroMemory(ScrBuffer, sizeof(ScrBuffer));

    SdPortBuildCommand(&Request,
                       SDACMD_SEND_SCR,
                       0,
                       SDRT_1,
                       SDTT_SINGLE_BLOCK,
                       SDTD_READ);
    Request.Command.CmdClass = SDCC_APP_CMD;

    Status = SdPortSendCommandWithData(FdoExtension, SlotExtension, &Request,
                                        ScrBuffer, 8, 1);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeSdCard: ACMD51 failed (0x%08lx)\n", Status);
        /* Non-fatal: some cards may not support SCR read cleanly */
    }
    else
    {
        SdPortParseScr(ScrBuffer, &SlotExtension->Scr);
        DPRINT1("SdPortInitializeSdCard: SCR spec=%u spec3=%u busWidths=0x%x\n",
               SlotExtension->Scr.SdSpec,
               SlotExtension->Scr.SdSpec3,
               SlotExtension->Scr.SdBusWidths);
    }

    /*
     * Step 7: ACMD6 - SET_BUS_WIDTH
     * Switch to 4-bit data bus if the card and host both support it.
     */
    if (SlotExtension->Scr.SdBusWidths & SD_SCR_BUS_WIDTH_4)
    {
        SdPortBuildCommand(&Request,
                           SDACMD_SET_BUS_WIDTH,
                           SD_ACMD6_BUS_WIDTH_4,
                           SDRT_1,
                           SDTT_CMD_ONLY,
                           SDTD_UNSPECIFIED);

        Status = SdPortSendAppCommand(FdoExtension, SlotExtension, &Request,
                                       SlotExtension->Rca);
        if (NT_SUCCESS(Status))
        {
            Status = SdPortSetBusWidth(FdoExtension, SlotExtension, 4);
            if (NT_SUCCESS(Status))
            {
                SlotExtension->CurrentBusWidth = 4;
                DPRINT1("SdPortInitializeSdCard: Switched to 4-bit bus\n");
            }
        }
        else
        {
            DPRINT1("SdPortInitializeSdCard: ACMD6 failed (0x%08lx)\n", Status);
        }
    }

    /*
     * Step 8: CMD16 - SET_BLOCKLEN
     * Set the block length to 512 bytes. This is required for SDSC cards;
     * SDHC/SDXC cards have a fixed 512-byte block length.
     */
    SdPortBuildCommand(&Request,
                       SDCMD_SET_BLOCKLEN,
                       SD_DEFAULT_BLOCK_SIZE,
                       SDRT_1,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeSdCard: CMD16 failed (0x%08lx)\n", Status);
        /* Non-fatal for SDHC cards */
    }

    /* Try to negotiate higher speed */
    SdPortSetBusSpeed(FdoExtension, SlotExtension);

    SlotExtension->Initialized = TRUE;
    DPRINT1("SdPortInitializeSdCard: Card initialization complete\n");
    return STATUS_SUCCESS;
}

/**
 * @brief Run the full eMMC card initialization sequence.
 *
 * Performs CMD1 loop (SEND_OP_COND with sector mode) to negotiate OCR,
 * then CMD2 (ALL_SEND_CID), CMD3 (SET_RELATIVE_ADDR), CMD9 (SEND_CSD),
 * CMD7 (SELECT_CARD), CMD8 (read EXT_CSD), CMD6 (SWITCH bus width to
 * 8-bit or 4-bit), CMD16 (SET_BLOCKLEN), and bus speed negotiation.
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in,out] SlotExtension  Pointer to the slot extension for the card.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SdPortInitializeEmmcCard(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension)
{
    SDPORT_REQUEST Request;
    ULONG Ocr;
    ULONG Argument;
    NTSTATUS Status;
    ULONGLONG Start;
    ULONGLONG Deadline;
    BOOLEAN Ready;
    BOOLEAN First;

    DPRINT1("SdPortInitializeEmmcCard: Slot %u\n", SlotExtension->SlotIndex);

    /*
     * Step 1: CMD1 - SEND_OP_COND (MMC/eMMC)
     */
    Argument = SD_OCR_VDD_RANGE | MMC_OCR_SECTOR_MODE;

    Start = KeQueryInterruptTime();
    Deadline = Start + 10000000ULL;
    Ocr = 0;
    Ready = FALSE;
    First = TRUE;

    while (TRUE)
    {
        SdPortBuildCommand(&Request,
                           SDCMD_SEND_OP_COND,
                           Argument,
                           SDRT_3,
                           SDTT_CMD_ONLY,
                           SDTD_UNSPECIFIED);

        Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_SD_CMD_TIMEOUT && First)
            {
                DPRINT1("SdPortInitializeEmmcCard: CMD1 not recognized\n");
                return Status;
            }
            DPRINT1("SdPortInitializeEmmcCard: CMD1 failed (0x%08lx)\n", Status);
            return Status;
        }

        First = FALSE;
        Ocr = Request.Response[0];
        if (Ocr & MMC_OCR_BUSY)
        {
            Ready = TRUE;
            break;
        }

        if (KeQueryInterruptTime() >= Deadline)
        {
            break;
        }

        SdPortDelayMs(10);
    }

    if (!Ready)
    {
        DPRINT1("SdPortInitializeEmmcCard: CMD1 timeout (1s deadline)\n");
        return STATUS_SD_CMD_TIMEOUT;
    }

    /* Determine addressing mode */
    if (Ocr & MMC_OCR_SECTOR_MODE)
    {
        SlotExtension->HighCapacity = TRUE;
        DPRINT1("SdPortInitializeEmmcCard: Sector addressing mode\n");
    }
    else
    {
        SlotExtension->HighCapacity = FALSE;
    }

    SlotExtension->CardType = SdCardTypeEmmc;

    /*
     * Step 2: CMD2 - ALL_SEND_CID
     */
    SdPortBuildCommand(&Request,
                       SDCMD_ALL_SEND_CID,
                       0,
                       SDRT_2,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeEmmcCard: CMD2 failed (0x%08lx)\n", Status);
        return Status;
    }

    SdPortParseCid(Request.Response, &SlotExtension->Cid);

    /*
     * Step 3: CMD3 - SET_RELATIVE_ADDR (eMMC: host assigns the RCA)
     * For eMMC, the host assigns an RCA. We use 0x0001 (shifted to bits[31:16]).
     */
    SlotExtension->Rca = 0x00010000;

    SdPortBuildCommand(&Request,
                       SDCMD_SEND_RELATIVE_ADDR,
                       SlotExtension->Rca,
                       SDRT_1,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeEmmcCard: CMD3 failed (0x%08lx)\n", Status);
        return Status;
    }

    {
        ULONGLONG Cmd13Start;
        ULONGLONG Cmd13Deadline;
        BOOLEAN InStandby = FALSE;

        Cmd13Start = KeQueryInterruptTime();
        Cmd13Deadline = Cmd13Start + 1000000ULL;

        while (TRUE)
        {
            SdPortBuildCommand(&Request,
                               SDCMD_SEND_STATUS,
                               SlotExtension->Rca,
                               SDRT_1,
                               SDTT_CMD_ONLY,
                               SDTD_UNSPECIFIED);

            Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SdPortInitializeEmmcCard: CMD13 failed (0x%08lx)\n",
                        Status);
                return Status;
            }

            if (SD_GET_STATE(Request.Response[0]) == SD_STATE_STANDBY)
            {
                InStandby = TRUE;
                break;
            }

            if (KeQueryInterruptTime() >= Cmd13Deadline)
            {
                break;
            }

            SdPortDelayMs(2);
        }

        if (!InStandby)
        {
            DPRINT1("SdPortInitializeEmmcCard: card did not reach STANDBY in 100ms (state=%lu)\n",
                    SD_GET_STATE(Request.Response[0]));
        }
    }

    /*
     * Step 4: CMD9 - SEND_CSD
     */
    SdPortBuildCommand(&Request,
                       SDCMD_SEND_CSD,
                       SlotExtension->Rca,
                       SDRT_2,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeEmmcCard: CMD9 failed (0x%08lx)\n", Status);
        return Status;
    }

    /* eMMC CSD is always version 1 format */
    SdPortParseCsdV1(Request.Response, &SlotExtension->Csd);

    /*
     * Step 5: CMD7 - SELECT_CARD
     */
    SdPortBuildCommand(&Request,
                       SDCMD_SELECT_CARD,
                       SlotExtension->Rca,
                       SDRT_1B,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeEmmcCard: CMD7 failed (0x%08lx)\n", Status);
        return Status;
    }

    /* Increase clock to default speed now that card is selected */
    Status = SdPortSetSlotClock(FdoExtension, SlotExtension, SD_DEFAULT_SPEED_KHZ);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeEmmcCard: Set clock failed (0x%08lx)\n", Status);
    }

    /*
     * Step 6: CMD8 - SEND_EXT_CSD (eMMC specific)
     * Read the 512-byte Extended CSD register.
     */
    RtlZeroMemory(SlotExtension->ExtCsd, sizeof(SlotExtension->ExtCsd));

    SdPortBuildCommand(&Request,
                       SDCMD_SEND_IF_COND,  /* CMD8 = SEND_EXT_CSD for eMMC */
                       0,
                       SDRT_1,
                       SDTT_SINGLE_BLOCK,
                       SDTD_READ);

    Status = SdPortSendCommandWithData(FdoExtension, SlotExtension, &Request,
                                        SlotExtension->ExtCsd, 512, 1);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeEmmcCard: CMD8 (EXT_CSD) failed (0x%08lx)\n", Status);
        return Status;
    }

    DPRINT1("SdPortInitializeEmmcCard: EXT_CSD rev=%u, device_type=0x%02x\n",
           SlotExtension->ExtCsd[EMMC_EXT_CSD_REV],
           SlotExtension->ExtCsd[EMMC_EXT_CSD_DEVICE_TYPE]);

    /*
     * Step 7: CMD6 - SWITCH (set bus width)
     * Try 8-bit if supported, otherwise 4-bit.
     *
     * CMD6 argument for eMMC:
     *   [25:24] = 0x03 (Write Byte access mode)
     *   [23:16] = index of the EXT_CSD byte
     *   [15:8]  = value to write
     *   [2:0]   = command set (0 = no change)
     */
    if (SlotExtension->Capabilities.EightBitSupported)
    {
        /* Switch to 8-bit bus */
        Argument = (0x03 << 24) |
                   (EMMC_EXT_CSD_BUS_WIDTH << 16) |
                   (EMMC_BUS_WIDTH_8 << 8);

        SdPortBuildCommand(&Request,
                           SDCMD_SWITCH_FUNC,
                           Argument,
                           SDRT_1B,
                           SDTT_CMD_ONLY,
                           SDTD_UNSPECIFIED);

        Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
        if (NT_SUCCESS(Status))
        {
            Status = SdPortVerifyEmmcSwitchStatus(FdoExtension, SlotExtension);
        }

        if (NT_SUCCESS(Status))
        {
            Status = SdPortSetBusWidth(FdoExtension, SlotExtension, 8);
            if (NT_SUCCESS(Status))
            {
                SlotExtension->CurrentBusWidth = 8;
                DPRINT1("SdPortInitializeEmmcCard: Switched to 8-bit bus\n");
            }
        }
        else
        {
            DPRINT1("SdPortInitializeEmmcCard: 8-bit switch failed, trying 4-bit\n");
            goto TryFourBit;
        }
    }
    else
    {
TryFourBit:
        /* Switch to 4-bit bus */
        Argument = (0x03 << 24) |
                   (EMMC_EXT_CSD_BUS_WIDTH << 16) |
                   (EMMC_BUS_WIDTH_4 << 8);

        SdPortBuildCommand(&Request,
                           SDCMD_SWITCH_FUNC,
                           Argument,
                           SDRT_1B,
                           SDTT_CMD_ONLY,
                           SDTD_UNSPECIFIED);

        Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
        if (NT_SUCCESS(Status))
        {
            Status = SdPortVerifyEmmcSwitchStatus(FdoExtension, SlotExtension);
        }

        if (NT_SUCCESS(Status))
        {
            Status = SdPortSetBusWidth(FdoExtension, SlotExtension, 4);
            if (NT_SUCCESS(Status))
            {
                SlotExtension->CurrentBusWidth = 4;
                DPRINT1("SdPortInitializeEmmcCard: Switched to 4-bit bus\n");
            }
        }
    }

    /*
     * Step 8: CMD16 - SET_BLOCKLEN (512 bytes)
     */
    SdPortBuildCommand(&Request,
                       SDCMD_SET_BLOCKLEN,
                       SD_DEFAULT_BLOCK_SIZE,
                       SDRT_1,
                       SDTT_CMD_ONLY,
                       SDTD_UNSPECIFIED);

    Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortInitializeEmmcCard: CMD16 failed (0x%08lx)\n", Status);
    }

    /* Try to negotiate higher speed for eMMC */
    SdPortSetBusSpeed(FdoExtension, SlotExtension);

    SlotExtension->Initialized = TRUE;
    DPRINT1("SdPortInitializeEmmcCard: Card initialization complete\n");
    return STATUS_SUCCESS;
}

/**
 * @brief Negotiate the highest bus speed supported by both card and host.
 *
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in,out] SlotExtension  Pointer to the slot extension for the card.
 *
 */
NTSTATUS
SdPortSetBusSpeed(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension)
{
    SDPORT_REQUEST Request;
    SDPORT_BUS_OPERATION BusOp;
    ULONG Argument;
    NTSTATUS Status = STATUS_SUCCESS;

    /*
     * For eMMC cards, negotiate high-speed or HS200 via EXT_CSD HS_TIMING.
     */
    if (SlotExtension->CardType == SdCardTypeEmmc)
    {
        UCHAR DeviceType = SlotExtension->ExtCsd[EMMC_EXT_CSD_DEVICE_TYPE];

        /* Try HS200 first if both sides support it */
        if ((DeviceType & EMMC_DEVICE_TYPE_HS200_18) &&
            SlotExtension->Capabilities.Hs200Supported &&
            SlotExtension->Capabilities.V18Supported)
        {
            /* Switch signaling voltage to 1.8V */
            Status = SdPortSetSignalingVoltage(FdoExtension, SlotExtension, 1);
            if (NT_SUCCESS(Status))
            {
                /* Set HS200 timing */
                Argument = (0x03 << 24) |
                           (EMMC_EXT_CSD_HS_TIMING << 16) |
                           (EMMC_TIMING_HS200 << 8);

                SdPortBuildCommand(&Request,
                                   SDCMD_SWITCH_FUNC,
                                   Argument,
                                   SDRT_1B,
                                   SDTT_CMD_ONLY,
                                   SDTD_UNSPECIFIED);

                Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
                if (NT_SUCCESS(Status))
                {
                    Status = SdPortVerifyEmmcSwitchStatus(FdoExtension, SlotExtension);
                }

                if (NT_SUCCESS(Status))
                {
                    Status = SdPortSetSlotClock(FdoExtension, SlotExtension,
                                                MMC_HS200_KHZ);
                    if (NT_SUCCESS(Status))
                    {
                        NTSTATUS TuningStatus;

                        SlotExtension->CurrentSpeedMode = EMMC_TIMING_HS200;
                        SlotExtension->CurrentClockKhz = MMC_HS200_KHZ;

                        TuningStatus = SdPortExecuteTuning(FdoExtension,
                                                            SlotExtension, 21);
                        if (NT_SUCCESS(TuningStatus))
                        {
                            DPRINT1("SdPortSetBusSpeed: eMMC HS200 mode at 200MHz "
                                    "(tuning converged)\n");
                            return Status;
                        }

                        DPRINT1("SdPortSetBusSpeed: HS200 tuning failed (0x%08lx); "
                                "falling back to high speed\n", TuningStatus);
                    }
                }
            }
        }

        /* Try high-speed (52 MHz) */
        if (DeviceType & (EMMC_DEVICE_TYPE_HS_52 | EMMC_DEVICE_TYPE_HS_26))
        {
            Argument = (0x03 << 24) |
                       (EMMC_EXT_CSD_HS_TIMING << 16) |
                       (EMMC_TIMING_HIGH_SPEED << 8);

            SdPortBuildCommand(&Request,
                               SDCMD_SWITCH_FUNC,
                               Argument,
                               SDRT_1B,
                               SDTT_CMD_ONLY,
                               SDTD_UNSPECIFIED);

            Status = SdPortSendCommand(FdoExtension, SlotExtension, &Request);
            if (NT_SUCCESS(Status))
            {
                Status = SdPortVerifyEmmcSwitchStatus(FdoExtension, SlotExtension);
            }

            if (NT_SUCCESS(Status))
            {
                ULONG ClockKhz = (DeviceType & EMMC_DEVICE_TYPE_HS_52) ?
                                  MMC_HIGH_SPEED_KHZ : SD_DEFAULT_SPEED_KHZ;

                Status = SdPortSetSlotClock(FdoExtension, SlotExtension, ClockKhz);
                if (NT_SUCCESS(Status))
                {
                    SlotExtension->CurrentSpeedMode = EMMC_TIMING_HIGH_SPEED;
                    SlotExtension->CurrentClockKhz = ClockKhz;
                    DPRINT1("SdPortSetBusSpeed: eMMC HS mode at %lu kHz\n", ClockKhz);
                    return Status;
                }
            }
        }

        DPRINT1("SdPortSetBusSpeed: eMMC staying at default speed\n");
        return Status;
    }

    /*
     * For SD cards, use CMD6 (SWITCH_FUNC) to query and switch access mode.
     *
     * CMD6 argument for SD:
     *   Bit 31 = mode (0=check, 1=switch)
     *   Bits [3:0] = group 1 function (access mode)
     *     0x0 = Default Speed
     *     0x1 = High Speed
     *     0x2 = SDR50
     *     0x3 = SDR104
     *     0x4 = DDR50
     */

    /* Try high-speed mode if the host supports it */
    if (SlotExtension->Capabilities.HighSpeedSupported)
    {
        UCHAR SwitchStatus[64];
        BOOLEAN HsSupported = FALSE;
        BOOLEAN Sdr50Supported = FALSE;
        BOOLEAN Sdr104Supported = FALSE;
        BOOLEAN Ddr50Supported = FALSE;

        Argument = 0x00FFFFF1;

        SdPortBuildCommand(&Request,
                           SDCMD_SWITCH_FUNC,
                           Argument,
                           SDRT_1,
                           SDTT_SINGLE_BLOCK,
                           SDTD_READ);

        RtlZeroMemory(SwitchStatus, sizeof(SwitchStatus));

        Status = SdPortSendCommandWithData(FdoExtension, SlotExtension,
                                            &Request, SwitchStatus, 64, 1);
        if (NT_SUCCESS(Status))
        {
            HsSupported     = (SwitchStatus[13] & 0x02) ? TRUE : FALSE;
            Sdr50Supported  = (SwitchStatus[13] & 0x04) ? TRUE : FALSE;
            Sdr104Supported = (SwitchStatus[13] & 0x08) ? TRUE : FALSE;
            Ddr50Supported  = (SwitchStatus[13] & 0x10) ? TRUE : FALSE;
            DPRINT1("SdPortSetBusSpeed: CMD6 check HS=%u SDR50=%u SDR104=%u DDR50=%u\n",
                    HsSupported, Sdr50Supported, Sdr104Supported, Ddr50Supported);

            (VOID)Sdr50Supported;
            (VOID)Sdr104Supported;
            (VOID)Ddr50Supported;
        }
        else
        {
            DPRINT1("SdPortSetBusSpeed: CMD6 check-mode failed (0x%08lx)\n",
                    Status);
        }

        if (HsSupported)
        {
            Argument = 0x80FFFFF1;

            SdPortBuildCommand(&Request,
                               SDCMD_SWITCH_FUNC,
                               Argument,
                               SDRT_1,
                               SDTT_SINGLE_BLOCK,
                               SDTD_READ);

            RtlZeroMemory(SwitchStatus, sizeof(SwitchStatus));

            Status = SdPortSendCommandWithData(FdoExtension, SlotExtension,
                                                &Request, SwitchStatus, 64, 1);
            if (NT_SUCCESS(Status))
            {
                /*
                 */
                UCHAR SelectedFunction = SwitchStatus[16] & 0x0F;
                if (SelectedFunction == 1)
                {
                    /* Tell the host controller to switch speed mode */
                    RtlZeroMemory(&BusOp, sizeof(BusOp));
                    BusOp.Type = SdSetBusSpeed;
                    BusOp.Parameters.SpeedMode = 1; /* High speed */

                    if (FdoExtension->MiniportInitData.IssueBusOperation == NULL)
                    {
                        Status = STATUS_SUCCESS;
                    }
                    else
                    {
                        Status = FdoExtension->MiniportInitData.IssueBusOperation(
                            FdoExtension->MiniportPrivateExtension,
                            &BusOp);
                    }
                    if (NT_SUCCESS(Status))
                    {
                        Status = SdPortSetSlotClock(FdoExtension, SlotExtension,
                                                    SD_HIGH_SPEED_KHZ);
                        if (NT_SUCCESS(Status))
                        {
                            SlotExtension->CurrentSpeedMode = 1;
                            SlotExtension->CurrentClockKhz = SD_HIGH_SPEED_KHZ;
                            DPRINT1("SdPortSetBusSpeed: SD High Speed at 50MHz\n");
                            return Status;
                        }
                    }
                    else
                    {
                        DPRINT1("SdPortSetBusSpeed: IssueBusOperation failed (0x%08lx)\n",
                                Status);
                    }
                }
                else
                {
                    DPRINT1("SdPortSetBusSpeed: card did not confirm HS switch (func=%u)\n",
                            SelectedFunction);
                }
            }
            else
            {
                DPRINT1("SdPortSetBusSpeed: CMD6 switch-mode failed (0x%08lx)\n",
                        Status);
            }
        }
    }

    /* Stay at default speed */
    SlotExtension->CurrentSpeedMode = 0;
    SlotExtension->CurrentClockKhz = SD_DEFAULT_SPEED_KHZ;
    DPRINT1("SdPortSetBusSpeed: Staying at default speed (25MHz)\n");
    return Status;
}
