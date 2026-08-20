/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2835 "SDHost" controller backend (Raspberry Pi boot SD slot)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdhost.h"

#define NDEBUG
#include <debug.h>

static ULONG
SdHostReadReg(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)FdoExtension->RegisterBase + Register));
}

static VOID
SdHostWriteReg(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG Register, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)FdoExtension->RegisterBase + Register), Value);
}

static VOID
SdHostDelayMs(_In_ ULONG Milliseconds)
{
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        KeStallExecutionProcessor(Milliseconds * 1000);
    }
    else
    {
        LARGE_INTEGER Delay;

        Delay.QuadPart = -(LONGLONG)Milliseconds * 10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

/* Same NTSTATUS conventions as SdBusInterruptErrorToStatus so enumeration fallbacks work */
static NTSTATUS
SdHostErrorToStatus(_In_ ULONG HostStatus)
{
    if (HostStatus & SDHOST_HSTS_CMD_TIME_OUT)
    {
        return STATUS_SD_CMD_TIMEOUT;
    }
    if (HostStatus & SDHOST_HSTS_CRC7_ERROR)
    {
        return STATUS_SD_CMD_CRC_ERROR;
    }
    if (HostStatus & SDHOST_HSTS_REW_TIME_OUT)
    {
        return STATUS_SD_DATA_TIMEOUT;
    }
    if (HostStatus & SDHOST_HSTS_CRC16_ERROR)
    {
        return STATUS_SD_DATA_CRC_ERROR;
    }
    return STATUS_SD_IO_ERROR;
}

NTSTATUS
SdHostSetClock(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG TargetClockKhz)
{
    ULONG Divisor;
    ULONG ActualClockKhz;

    if (FdoExtension == NULL || FdoExtension->RegisterBase == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (TargetClockKhz == 0)
    {
        /* The SDHost core cannot gate the card clock; park it at the slowest rate instead */
        SdHostWriteReg(FdoExtension, SDHOST_SDCDIV, SDHOST_MAX_CDIV);
        FdoExtension->SdHostCdiv = SDHOST_MAX_CDIV;
        InterlockedExchange(&FdoExtension->CurrentClockKhz, 0);
        return STATUS_SUCCESS;
    }

    if (TargetClockKhz > SDHOST_BASE_CLOCK_KHZ)
    {
        TargetClockKhz = SDHOST_BASE_CLOCK_KHZ;
    }

    /* SDclk = 250 MHz / (SDCDIV + 2); pick the divisor that does not overclock the target */
    Divisor = SDHOST_BASE_CLOCK_KHZ / TargetClockKhz;
    if (Divisor < 2)
    {
        Divisor = 2;
    }
    if ((SDHOST_BASE_CLOCK_KHZ / Divisor) > TargetClockKhz)
    {
        Divisor++;
    }
    Divisor -= 2;
    if (Divisor > SDHOST_MAX_CDIV)
    {
        Divisor = SDHOST_MAX_CDIV;
    }

    SdHostWriteReg(FdoExtension, SDHOST_SDCDIV, Divisor);
    FdoExtension->SdHostCdiv = Divisor;
    ActualClockKhz = SDHOST_BASE_CLOCK_KHZ / (Divisor + 2);

    /* ~500 ms data timeout, counted in SD clocks */
    SdHostWriteReg(FdoExtension, SDHOST_SDTOUT, ActualClockKhz * 500);
    InterlockedExchange(&FdoExtension->CurrentClockKhz, (LONG)ActualClockKhz);

    return STATUS_SUCCESS;
}

VOID
SdHostSetBusWidth(_In_ PFDO_EXTENSION FdoExtension, _In_ UCHAR BusWidth)
{
    if (BusWidth == 4)
    {
        FdoExtension->SdHostHcfg |= SDHOST_CFG_WIDE_EXT_BUS;
    }
    else
    {
        FdoExtension->SdHostHcfg &= ~SDHOST_CFG_WIDE_EXT_BUS;
    }
    SdHostWriteReg(FdoExtension, SDHOST_SDHCFG, FdoExtension->SdHostHcfg);
    FdoExtension->CurrentBusWidth = (BusWidth == 4) ? 4 : 1;
}

static VOID
SdHostHardwareInit(_In_ PFDO_EXTENSION FdoExtension)
{
    ULONG Edm;

    SdHostWriteReg(FdoExtension, SDHOST_SDVDD, 0);
    SdHostWriteReg(FdoExtension, SDHOST_SDCMD, 0);
    SdHostWriteReg(FdoExtension, SDHOST_SDARG, 0);
    SdHostWriteReg(FdoExtension, SDHOST_SDTOUT, SDHOST_INIT_TIMEOUT_CLOCKS);
    SdHostWriteReg(FdoExtension, SDHOST_SDCDIV, 0);
    SdHostWriteReg(FdoExtension, SDHOST_SDHSTS, SDHOST_HSTS_CLEAR_MASK);
    SdHostWriteReg(FdoExtension, SDHOST_SDHCFG, 0);
    SdHostWriteReg(FdoExtension, SDHOST_SDHBCT, 0);
    SdHostWriteReg(FdoExtension, SDHOST_SDHBLC, 0);

    /* Limit FIFO usage to 4-word thresholds to dodge a silicon overrun bug */
    Edm = SdHostReadReg(FdoExtension, SDHOST_SDEDM);
    Edm &=
        ~((SDHOST_EDM_THRESHOLD_MASK << SDHOST_EDM_READ_THRESHOLD_SHIFT) |
          (SDHOST_EDM_THRESHOLD_MASK << SDHOST_EDM_WRITE_THRESHOLD_SHIFT));
    Edm |= (SDHOST_FIFO_READ_THRESHOLD << SDHOST_EDM_READ_THRESHOLD_SHIFT) |
           (SDHOST_FIFO_WRITE_THRESHOLD << SDHOST_EDM_WRITE_THRESHOLD_SHIFT);
    SdHostWriteReg(FdoExtension, SDHOST_SDEDM, Edm);
    SdHostDelayMs(20);

    SdHostWriteReg(FdoExtension, SDHOST_SDVDD, SDHOST_VDD_POWER_ON);
    SdHostDelayMs(20);

    SdHostWriteReg(FdoExtension, SDHOST_SDHCFG, FdoExtension->SdHostHcfg);
    SdHostWriteReg(FdoExtension, SDHOST_SDCDIV, FdoExtension->SdHostCdiv);
}

NTSTATUS
SdHostInitializeController(_In_ PFDO_EXTENSION FdoExtension)
{
    /*
     * Publish the SDHCI-equivalent capabilities used by shared enumeration:
     * 250 MHz base clock, 3.3 V, high speed, and PIO-only 4-bit transfers.
     */
    FdoExtension->SpecVersion = SDHCI_SPEC_300;
    FdoExtension->HostCapabilities =
        SDHCI_CAP_HIGH_SPEED | SDHCI_CAP_VOLTAGE_330 | ((SDHOST_BASE_CLOCK_KHZ / 1000) << SDHCI_CAP_BASE_CLK_SHIFT);
    FdoExtension->HostCapabilities2 = 0;
    FdoExtension->MaxClockFrequency = SDHOST_BASE_CLOCK_KHZ;
    FdoExtension->UseAdma2 = FALSE;
    FdoExtension->UseSdma = FALSE;
    FdoExtension->CurrentBusWidth = 1;
    /* The SDHost slot has no card-detect or write-protect signals; the boot SD card is treated as always present */
    FdoExtension->NonRemovable = TRUE;

    /*
     * SLOW_CARD pins the clock to the 11-bit SDCDIV divisor in data mode too,
     * and BUSY_IRPT_EN makes SDHSTS latch busy-wait completion for polling.
     * No interrupt is ever connected for this host type, so the IRPT_EN bit
     * only arms the status latch.
     */
    FdoExtension->SdHostHcfg = SDHOST_CFG_WIDE_INT_BUS | SDHOST_CFG_SLOW_CARD | SDHOST_CFG_BUSY_IRPT_EN;
    FdoExtension->SdHostCdiv = SDHOST_MAX_CDIV;
    InterlockedExchange(&FdoExtension->CurrentClockKhz, 0);
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);

    SdHostHardwareInit(FdoExtension);
    return SdHostSetClock(FdoExtension, SD_INIT_CLOCK_KHZ);
}

/*
 * Error recovery equivalent of the SDHCI CMD/DAT circuit resets. VDD is kept
 * on so the card retains its state: command-phase probe timeouts (CMD8, CMD5)
 * during enumeration must not power-cycle a half-initialized card. Only when
 * the data FSM refuses to return to idle is the full power-on init re-run.
 */
NTSTATUS
SdHostReset(_In_ PFDO_EXTENSION FdoExtension)
{
    ULONG Edm;
    ULONG Fsm;
    ULONG CurrentClockKhz;
    ULONG Timeout;

    SdHostWriteReg(FdoExtension, SDHOST_SDCMD, 0);
    SdHostWriteReg(FdoExtension, SDHOST_SDHSTS, SDHOST_HSTS_CLEAR_MASK);
    SdHostWriteReg(FdoExtension, SDHOST_SDHBCT, 0);
    SdHostWriteReg(FdoExtension, SDHOST_SDHBLC, 0);

    Timeout = 1000;
    for (;;)
    {
        Edm = SdHostReadReg(FdoExtension, SDHOST_SDEDM);
        Fsm = Edm & SDHOST_EDM_FSM_MASK;
        if (Fsm == SDHOST_EDM_FSM_IDENTMODE || Fsm == SDHOST_EDM_FSM_DATAMODE)
        {
            break;
        }
        if (Timeout == 0)
        {
            DPRINT1(
                "[SDHOST] FSM stuck in state 0x%lx during reset; re-running power-on init (card state is lost)\n", Fsm);
            SdHostHardwareInit(FdoExtension);
            break;
        }
        SdHostWriteReg(FdoExtension, SDHOST_SDEDM, Edm | SDHOST_EDM_FORCE_DATA_MODE);
        KeStallExecutionProcessor(10);
        Timeout--;
    }

    /* Restore the shadowed configuration and the timeout for the current clock */
    SdHostWriteReg(FdoExtension, SDHOST_SDHCFG, FdoExtension->SdHostHcfg);
    SdHostWriteReg(FdoExtension, SDHOST_SDCDIV, FdoExtension->SdHostCdiv);
    CurrentClockKhz = (ULONG)InterlockedCompareExchange(&FdoExtension->CurrentClockKhz, 0, 0);
    SdHostWriteReg(
        FdoExtension, SDHOST_SDTOUT, CurrentClockKhz != 0 ? CurrentClockKhz * 500 : SDHOST_INIT_TIMEOUT_CLOCKS);
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdHostWaitCommandIdle(_In_ PFDO_EXTENSION FdoExtension, _Out_ PULONG SdCmd)
{
    ULONG Timeout;

    Timeout = SD_CMD_TIMEOUT_MS * 100;
    for (;;)
    {
        *SdCmd = SdHostReadReg(FdoExtension, SDHOST_SDCMD);
        if (!(*SdCmd & SDHOST_CMD_NEW_FLAG))
        {
            return STATUS_SUCCESS;
        }
        if (Timeout == 0)
        {
            return STATUS_IO_TIMEOUT;
        }
        KeStallExecutionProcessor(10);
        Timeout--;
    }
}

static NTSTATUS
SdHostIssueCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _In_ ULONG DataDirection,
    _Out_writes_opt_(4) PULONG Response)
{
    ULONG SdCmd;
    ULONG HostStatus;
    ULONG ResponseKind;
    ULONG ErrorBits;
    ULONG Timeout;
    BOOLEAN UseBusy;
    NTSTATUS Status;

    Status = SdHostWaitCommandIdle(FdoExtension, &SdCmd);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[SDHOST] CMD%u: previous command never completed (SDCMD=0x%08lx)\n", CommandIndex, SdCmd);
        (void)SdHostReset(FdoExtension);
        return STATUS_IO_TIMEOUT;
    }

    /* Drop stale write-1-clear status before starting a fresh command */
    HostStatus = SdHostReadReg(FdoExtension, SDHOST_SDHSTS);
    if (HostStatus & SDHOST_HSTS_CLEAR_MASK)
    {
        SdHostWriteReg(FdoExtension, SDHOST_SDHSTS, HostStatus & SDHOST_HSTS_CLEAR_MASK);
    }

    SdHostWriteReg(FdoExtension, SDHOST_SDARG, Argument);

    ResponseKind = CommandFlags & SDHCI_CMD_RESP_MASK;
    UseBusy = (ResponseKind == SDHCI_CMD_RESP_48_BUSY && DataDirection == 0);
    SdCmd = CommandIndex & SDHOST_CMD_INDEX_MASK;
    if (ResponseKind == SDHCI_CMD_RESP_NONE)
    {
        SdCmd |= SDHOST_CMD_NO_RESPONSE;
    }
    if (ResponseKind == SDHCI_CMD_RESP_136)
    {
        SdCmd |= SDHOST_CMD_LONG_RESPONSE;
    }
    if (UseBusy)
    {
        SdCmd |= SDHOST_CMD_BUSYWAIT;
    }
    SdCmd |= DataDirection;
    SdHostWriteReg(FdoExtension, SDHOST_SDCMD, SdCmd | SDHOST_CMD_NEW_FLAG);

    if (UseBusy)
    {
        /*
         * BUSY_IRPT_EN latches SDHSTS_BUSY_IRPT once DAT0 releases. Poll the
         * latch because this backend does not connect an interrupt.
         */
        Timeout = SD_DATA_TIMEOUT_MS * 100;
        for (;;)
        {
            HostStatus = SdHostReadReg(FdoExtension, SDHOST_SDHSTS);
            if (HostStatus & SDHOST_HSTS_BUSY_IRPT)
            {
                SdHostWriteReg(FdoExtension, SDHOST_SDHSTS, SDHOST_HSTS_BUSY_IRPT);
                break;
            }
            if (HostStatus & SDHOST_HSTS_ERROR_MASK)
            {
                break;
            }
            SdCmd = SdHostReadReg(FdoExtension, SDHOST_SDCMD);
            if (!(SdCmd & SDHOST_CMD_NEW_FLAG) && (SdCmd & SDHOST_CMD_FAIL_FLAG))
            {
                break;
            }
            if (Timeout == 0)
            {
                DPRINT1("[SDHOST] CMD%u busy-wait timed out (SDHSTS=0x%08lx)\n", CommandIndex, HostStatus);
                (void)SdHostReset(FdoExtension);
                return STATUS_IO_TIMEOUT;
            }
            KeStallExecutionProcessor(10);
            Timeout--;
        }
    }

    Status = SdHostWaitCommandIdle(FdoExtension, &SdCmd);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[SDHOST] CMD%u completion timed out (SDCMD=0x%08lx)\n", CommandIndex, SdCmd);
        (void)SdHostReset(FdoExtension);
        return STATUS_IO_TIMEOUT;
    }

    if (SdCmd & SDHOST_CMD_FAIL_FLAG)
    {
        HostStatus = SdHostReadReg(FdoExtension, SDHOST_SDHSTS);
        if (HostStatus & SDHOST_HSTS_CLEAR_MASK)
        {
            SdHostWriteReg(FdoExtension, SDHOST_SDHSTS, HostStatus & SDHOST_HSTS_CLEAR_MASK);
        }

        /*
         * Hardware always validates CRC7. Ignore that result when the caller
         * disabled the advisory check for R2/R3/R4-style responses.
         */
        ErrorBits = HostStatus & SDHOST_HSTS_ERROR_MASK;
        if (!(CommandFlags & SDHCI_CMD_CRC_CHECK))
        {
            ErrorBits &= ~SDHOST_HSTS_CRC7_ERROR;
        }

        /*
         * Command-phase failures leave the FSM healthy: return without a
         * reset so expected probe timeouts (CMD8 on SD v1, CMD5 on memory
         * cards) stay cheap and do not disturb the half-enumerated card.
         */
        if (ErrorBits != 0)
        {
            return SdHostErrorToStatus(ErrorBits);
        }
    }

    if (Response != NULL)
    {
        if (ResponseKind == SDHCI_CMD_RESP_136)
        {
            ULONG Rsp0 = SdHostReadReg(FdoExtension, SDHOST_SDRSP0);
            ULONG Rsp1 = SdHostReadReg(FdoExtension, SDHOST_SDRSP1);
            ULONG Rsp2 = SdHostReadReg(FdoExtension, SDHOST_SDRSP2);
            ULONG Rsp3 = SdHostReadReg(FdoExtension, SDHOST_SDRSP3);

            /*
             * SDRSP0..3 hold the raw 128-bit payload (bits 31:0 .. 127:96,
             * internal CRC byte included). SDHCI response registers hold the
             * same payload shifted right by 8 with the CRC byte dropped, and
             * SdBusParseCid/SdBusParseCsd expect that layout; convert here.
             */
            Response[0] = (Rsp1 << 24) | (Rsp0 >> 8);
            Response[1] = (Rsp2 << 24) | (Rsp1 >> 8);
            Response[2] = (Rsp3 << 24) | (Rsp2 >> 8);
            Response[3] = Rsp3 >> 8;
        }
        else if (ResponseKind != SDHCI_CMD_RESP_NONE)
        {
            Response[0] = SdHostReadReg(FdoExtension, SDHOST_SDRSP0);
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
SdHostSendCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response)
{
    return SdHostIssueCommand(FdoExtension, CommandIndex, Argument, CommandFlags, 0, Response);
}

static NTSTATUS
SdHostTransferPio(_In_ PFDO_EXTENSION FdoExtension, _In_ BOOLEAN IsRead, _Inout_ PUCHAR Buffer, _In_ ULONG TotalBytes)
{
    ULONG RemainingWords;
    ULONG WaitBudget;

    RemainingWords = TotalBytes / sizeof(ULONG);
    WaitBudget = SD_DATA_TIMEOUT_MS * 100;

    while (RemainingWords > 0)
    {
        ULONG Edm = SdHostReadReg(FdoExtension, SDHOST_SDEDM);
        ULONG Level = (Edm >> SDHOST_EDM_FIFO_FILL_SHIFT) & SDHOST_EDM_FIFO_FILL_MASK;
        ULONG Burst = IsRead ? Level : (SDHOST_FIFO_WORDS - Level);

        if (Burst == 0)
        {
            ULONG HostStatus = SdHostReadReg(FdoExtension, SDHOST_SDHSTS);

            if (HostStatus & SDHOST_HSTS_TRANSFER_ERROR_MASK)
            {
                return SdHostErrorToStatus(HostStatus & SDHOST_HSTS_TRANSFER_ERROR_MASK);
            }
            if (WaitBudget == 0)
            {
                DPRINT1(
                    "[SDHOST] PIO %s stalled with %lu words left (EDM=0x%08lx HSTS=0x%08lx)\n",
                    IsRead ? "read" : "write", RemainingWords, Edm, HostStatus);
                return STATUS_IO_TIMEOUT;
            }
            KeStallExecutionProcessor(10);
            WaitBudget--;
            continue;
        }

        if (Burst > RemainingWords)
        {
            Burst = RemainingWords;
        }
        RemainingWords -= Burst;
        while (Burst > 0)
        {
            ULONG Value;

            if (IsRead)
            {
                Value = SdHostReadReg(FdoExtension, SDHOST_SDDATA);
                RtlCopyMemory(Buffer, &Value, sizeof(Value));
            }
            else
            {
                RtlCopyMemory(&Value, Buffer, sizeof(Value));
                SdHostWriteReg(FdoExtension, SDHOST_SDDATA, Value);
            }
            Buffer += sizeof(ULONG);
            Burst--;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
SdHostWaitTransferComplete(_In_ PFDO_EXTENSION FdoExtension)
{
    ULONG Timeout;

    Timeout = SD_DATA_TIMEOUT_MS * 100;
    for (;;)
    {
        ULONG Edm = SdHostReadReg(FdoExtension, SDHOST_SDEDM);
        ULONG Fsm = Edm & SDHOST_EDM_FSM_MASK;

        if (Fsm == SDHOST_EDM_FSM_IDENTMODE || Fsm == SDHOST_EDM_FSM_DATAMODE)
        {
            return STATUS_SUCCESS;
        }
        if (Fsm == SDHOST_EDM_FSM_READWAIT || Fsm == SDHOST_EDM_FSM_READDATA || Fsm == SDHOST_EDM_FSM_WRITESTART1)
        {
            /* All data words are consumed; yank a lingering FSM back to data mode */
            SdHostWriteReg(FdoExtension, SDHOST_SDEDM, Edm | SDHOST_EDM_FORCE_DATA_MODE);
            return STATUS_SUCCESS;
        }
        if (Timeout == 0)
        {
            DPRINT1(
                "[SDHOST] Transfer completion wait timed out (EDM=0x%08lx HSTS=0x%08lx)\n", Edm,
                SdHostReadReg(FdoExtension, SDHOST_SDHSTS));
            return STATUS_IO_TIMEOUT;
        }
        KeStallExecutionProcessor(10);
        Timeout--;
    }
}

NTSTATUS
SdHostExecuteDataCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _In_ BOOLEAN IsRead,
    _In_ BOOLEAN SendStopCommand,
    _Inout_updates_bytes_(BlockSize * BlockCount) PVOID Buffer,
    _In_ ULONG BlockSize,
    _In_ ULONG BlockCount,
    _Out_opt_ PULONG Response)
{
    ULONG LocalResponse[4];
    ULONG HostStatus;
    NTSTATUS Status;

    /* The FIFO is a 32-bit word port; partial words cannot be transferred */
    if (Buffer == NULL || BlockSize == 0 || BlockCount == 0 || (BlockSize % sizeof(ULONG)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(LocalResponse, sizeof(LocalResponse));

    /* Program the block geometry before the command starts the data phase */
    SdHostWriteReg(FdoExtension, SDHOST_SDHBCT, BlockSize);
    SdHostWriteReg(FdoExtension, SDHOST_SDHBLC, BlockCount);

    Status = SdHostIssueCommand(
        FdoExtension, CommandIndex, Argument, CommandFlags, IsRead ? SDHOST_CMD_READ_CMD : SDHOST_CMD_WRITE_CMD,
        LocalResponse);
    if (Response != NULL)
    {
        Response[0] = LocalResponse[0];
    }
    if (!NT_SUCCESS(Status))
    {
        (void)SdHostReset(FdoExtension);
        return Status;
    }

    Status = SdHostTransferPio(FdoExtension, IsRead, (PUCHAR)Buffer, BlockSize * BlockCount);
    if (NT_SUCCESS(Status))
    {
        Status = SdHostWaitTransferComplete(FdoExtension);
    }

    HostStatus = SdHostReadReg(FdoExtension, SDHOST_SDHSTS);
    if (NT_SUCCESS(Status) && (HostStatus & SDHOST_HSTS_TRANSFER_ERROR_MASK))
    {
        Status = SdHostErrorToStatus(HostStatus & SDHOST_HSTS_TRANSFER_ERROR_MASK);
    }
    if (HostStatus & SDHOST_HSTS_CLEAR_MASK)
    {
        SdHostWriteReg(FdoExtension, SDHOST_SDHSTS, HostStatus & SDHOST_HSTS_CLEAR_MASK);
    }

    if (SendStopCommand)
    {
        NTSTATUS StopStatus;

        /*
         * The engine has no auto-CMD12. Close open-ended multi-block
         * transfers explicitly, using R1b to cover write programming.
         */
        StopStatus = SdHostSendCommand(
            FdoExtension, SDCMD_STOP_TRANSMISSION, 0,
            SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK, NULL);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(StopStatus))
        {
            Status = StopStatus;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        (void)SdHostReset(FdoExtension);
    }

    return Status;
}

NTSTATUS
SdHostExecuteRequest(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PSDCMD_DESCRIPTOR CmdDesc,
    _In_ ULONG Argument,
    _Inout_opt_ PMDL Mdl,
    _In_ ULONG DataLength,
    _In_ ULONG BlockSize,
    _In_ USHORT RequestFlags,
    _Out_opt_ PULONG Response)
{
    USHORT CommandFlags;
    PVOID DataBuffer;
    BOOLEAN IsRead;
    BOOLEAN SendStop;
    NTSTATUS Status;

    CommandFlags = SdBusResponseTypeToFlags(CmdDesc->ResponseType);

    if (DataLength == 0 || Mdl == NULL || CmdDesc->TransferType == SDTT_CMD_ONLY)
    {
        /* SDCMD_BUSYWAIT is the hardware's DAT0 busy poll; use it for the explicit busy-wait contract too */
        if ((RequestFlags & SDRP_FLAG_WAIT_FOR_BUSY) && (CommandFlags & SDHCI_CMD_RESP_MASK) == SDHCI_CMD_RESP_48)
        {
            CommandFlags = (USHORT)((CommandFlags & ~SDHCI_CMD_RESP_MASK) | SDHCI_CMD_RESP_48_BUSY);
        }
        return SdHostSendCommand(FdoExtension, (UCHAR)CmdDesc->Cmd, Argument, CommandFlags, Response);
    }

    DataBuffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    if (DataBuffer == NULL)
    {
        DPRINT1("[SDHOST] Failed to map request MDL\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IsRead = (CmdDesc->TransferDirection == SDTD_READ);
    SendStop = (CmdDesc->TransferType == SDTT_MULTI_BLOCK);
    Status = SdHostExecuteDataCommand(
        FdoExtension, (UCHAR)CmdDesc->Cmd, Argument, CommandFlags, IsRead, SendStop, DataBuffer, BlockSize,
        DataLength / BlockSize, Response);

    if (NT_SUCCESS(Status) && !IsRead && (RequestFlags & SDRP_FLAG_WAIT_FOR_BUSY))
    {
        /*
         * There is no DAT0 level register. Treat the return to data mode and
         * a short settle delay as the SDHCI DATA_INHIBIT busy poll.
         */
        Status = SdHostWaitTransferComplete(FdoExtension);
        SdHostDelayMs(1);
    }

    return Status;
}
