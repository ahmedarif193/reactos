/*
 * PROJECT:     ReactOS SD Host Controller Miniport Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Standard SDHCI miniport implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This miniport implements the SD Host Controller Simplified Specification
 * (SDHCI) for controllers compliant with the standard register interface.
 * It provides hardware-specific callbacks to the sdport.sys port driver.
 */

#include "sdhc.h"

#define NDEBUG
#include <debug.h>

static VOID
SdhcApplyControllerState(
    _In_ PSDHC_EXTENSION Extension);

static
VOID
SdhcWriteIntStatusEnable(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG Value)
{
    Extension->SavedIntStatusEnable = Value;
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_INT_STATUS_ENABLE,
                  (USHORT)(Value & 0xFFFF));
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_INT_STATUS_ENABLE + 2,
                  (USHORT)((Value >> 16) & 0xFFFF));
}

static
VOID
SdhcWriteIntSignalEnable(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG Value)
{
    Extension->SavedIntSignalEnable = Value;
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_INT_SIGNAL_ENABLE,
                  (USHORT)(Value & 0xFFFF));
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_INT_SIGNAL_ENABLE + 2,
                  (USHORT)((Value >> 16) & 0xFFFF));
}

static
VOID
SdhcWriteTimeoutControl(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR Value)
{
    Extension->SavedTimeoutControl = Value;
    SDHCI_WRITE8(Extension->BaseAddress, SDHCI_TIMEOUT_CONTROL, Value);
}

static
VOID
SdhcWritePowerControl(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR Value)
{
    Extension->SavedPowerControl = Value;
    SDHCI_WRITE8(Extension->BaseAddress, SDHCI_POWER_CONTROL, Value);
}

static
VOID
SdhcWriteClockControl(
    _In_ PSDHC_EXTENSION Extension,
    _In_ USHORT Value)
{
    Extension->SavedClockControl = Value & (USHORT)~SDHCI_CLK_INT_CLK_STABLE;
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_CLOCK_CONTROL, Value);
}

static
VOID
SdhcWriteHostControl(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR Value)
{
    Extension->SavedHostControl = Value;
    SDHCI_WRITE8(Extension->BaseAddress, SDHCI_HOST_CONTROL, Value);
}

static
VOID
SdhcWriteHostControl2(
    _In_ PSDHC_EXTENSION Extension,
    _In_ USHORT Value)
{
    Extension->SavedHostControl2 = Value & (USHORT)~SDHCI_HC2_EXEC_TUNING;
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_HOST_CONTROL2, Value);
}

static
VOID
SdhcApplyControllerState(
    _In_ PSDHC_EXTENSION Extension)
{
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_INT_STATUS_ENABLE,
                  (USHORT)(Extension->SavedIntStatusEnable & 0xFFFF));
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_INT_STATUS_ENABLE + 2,
                  (USHORT)((Extension->SavedIntStatusEnable >> 16) & 0xFFFF));
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_INT_SIGNAL_ENABLE,
                  (USHORT)(Extension->SavedIntSignalEnable & 0xFFFF));
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_INT_SIGNAL_ENABLE + 2,
                  (USHORT)((Extension->SavedIntSignalEnable >> 16) & 0xFFFF));

    SDHCI_WRITE8(Extension->BaseAddress, SDHCI_TIMEOUT_CONTROL,
                 Extension->SavedTimeoutControl);

    SDHCI_WRITE8(Extension->BaseAddress, SDHCI_POWER_CONTROL,
                 Extension->SavedPowerControl);

    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_CLOCK_CONTROL,
                  Extension->SavedClockControl);

    SDHCI_WRITE8(Extension->BaseAddress, SDHCI_HOST_CONTROL,
                 Extension->SavedHostControl);
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_HOST_CONTROL2,
                  Extension->SavedHostControl2);
}

static const SDHC_QUIRK_ENTRY SDHC_QUIRKS[] = {
    { 0x1180, 0x0822, SDHC_QUIRK_POWER_ON_DELAY },
    { 0x1180, 0xE823, SDHC_QUIRK_POWER_ON_DELAY },
    { 0x8086, 0x0F14, SDHC_QUIRK_NO_HIGHSPEED_BIT },
    { 0x8086, 0x0F15, SDHC_QUIRK_NO_HIGHSPEED_BIT },
    { 0x8086, 0x0F16, SDHC_QUIRK_NO_HIGHSPEED_BIT },
    { 0x1217, 0x8520, SDHC_QUIRK_O2_VENDOR_UNLOCK },
    { 0x1217, 0x8521, SDHC_QUIRK_O2_VENDOR_UNLOCK },
    { 0x11AB, 0xA010, SDHC_QUIRK_NO_CLOCK_MULT },
    { 0x0000, 0x0000, 0 },
};

VOID
SdhcLookupQuirks(
    _In_ PSDHC_EXTENSION Extension,
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId)
{
    const SDHC_QUIRK_ENTRY *Entry;

    Extension->QuirkFlags = 0;

    if (VendorId == 0)
    {
        return;
    }

    for (Entry = &SDHC_QUIRKS[0]; Entry->VendorId != 0; Entry++)
    {
        if ((Entry->VendorId == 0xFFFF || Entry->VendorId == VendorId) &&
            (Entry->DeviceId == 0xFFFF || Entry->DeviceId == DeviceId))
        {
            Extension->QuirkFlags |= Entry->QuirkFlags;
        }
    }
}

/**
 * @brief Return the number of SD card slots on this controller.
 *
 * Standard SDHCI controllers typically have 1 slot.
 * Multi-slot controllers report via PCI BAR layout.
 *
 * @param Extension Per-slot miniport private data.
 * @param SlotCount Receives the number of available slots.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */

NTSTATUS
SdhcGetSlotCount(
    _In_ PSDHC_EXTENSION Extension,
    _Out_ PUCHAR SlotCount)
{
    UNREFERENCED_PARAMETER(Extension);

    *SlotCount = 1;
    return STATUS_SUCCESS;
}

/**
 * @brief Query the hardware capabilities of a given slot.
 *
 * Reads the SDHCI capability registers and populates the caller-supplied
 * capabilities structure with supported voltages, speeds, and features.
 *
 * @param Extension Per-slot miniport private data.
 * @param SlotIndex Zero-based index of the slot to query.
 * @param Capabilities Receives the slot capability flags.
 */

VOID
SdhcGetSlotCapabilities(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR SlotIndex,
    _Out_ PSDPORT_CAPABILITIES Capabilities)
{
    ULONG Cap, Cap2;

    UNREFERENCED_PARAMETER(SlotIndex);

    RtlZeroMemory(Capabilities, sizeof(SDPORT_CAPABILITIES));

    Cap = Extension->Capabilities;
    Cap2 = Extension->Capabilities2;

    Capabilities->MaximumBlockSize = SDHCI_MAX_BLOCK_LENGTH(Cap);
    Capabilities->MaximumBlockCount =
        (Extension->SpecVersion >= SDHCI_SPEC_410) ? 0x03FFFFFF : 0x0000FFFF;
    Capabilities->BaseClockFrequencyKhz = Extension->BaseClockKhz;

    Capabilities->HighSpeedSupported = (Cap & SDHCI_CAP_HIGH_SPEED) ? TRUE : FALSE;
    Capabilities->Adma2Supported = (Cap & SDHCI_CAP_ADMA2_SUPPORT) ? TRUE : FALSE;
    Capabilities->SdmaSupported = (Cap & SDHCI_CAP_SDMA_SUPPORT) ? TRUE : FALSE;
    Capabilities->V33Supported = (Cap & SDHCI_CAP_VOLTAGE_330) ? TRUE : FALSE;
    Capabilities->V30Supported = (Cap & SDHCI_CAP_VOLTAGE_300) ? TRUE : FALSE;
    Capabilities->V18Supported = (Cap & SDHCI_CAP_VOLTAGE_180) ? TRUE : FALSE;
    Capabilities->EightBitSupported = (Cap & SDHCI_CAP_8BIT_SUPPORT) ? TRUE : FALSE;
    Capabilities->SlotType = (UCHAR)((Cap & SDHCI_CAP_SLOT_TYPE_MASK) >> 30);

    Capabilities->Sdr50Supported = (Cap2 & SDHCI_CAP2_SDR50_SUPPORT) ? TRUE : FALSE;
    Capabilities->Sdr104Supported = (Cap2 & SDHCI_CAP2_SDR104_SUPPORT) ? TRUE : FALSE;
    Capabilities->Ddr50Supported = (Cap2 & SDHCI_CAP2_DDR50_SUPPORT) ? TRUE : FALSE;
    Capabilities->Hs400Supported = (Cap2 & SDHCI_CAP2_HS400_SUPPORT) ? TRUE : FALSE;
    Capabilities->DriverTypeASupported = (Cap2 & SDHCI_CAP2_DRIVER_TYPE_A) ? TRUE : FALSE;
    Capabilities->DriverTypeCSupported = (Cap2 & SDHCI_CAP2_DRIVER_TYPE_C) ? TRUE : FALSE;
    Capabilities->DriverTypeDSupported = (Cap2 & SDHCI_CAP2_DRIVER_TYPE_D) ? TRUE : FALSE;
    Capabilities->ClockMultiplier = (Cap2 & SDHCI_CAP2_CLK_MULTIPLIER_MASK) >>
                                    SDHCI_CAP2_CLK_MULTIPLIER_SHIFT;
    Capabilities->RetuneTimer = (Cap2 & SDHCI_CAP2_RETUNE_TIMER_MASK) >>
                                SDHCI_CAP2_RETUNE_TIMER_SHIFT;
    Capabilities->Adma3Supported = (Cap2 & (1UL << 27)) ? TRUE : FALSE;
}

/**
 * @brief Initialize a host controller slot for operation.
 *
 * Maps the register space, reads the controller version and capabilities,
 * performs a full software reset, enables interrupts, sets the initial
 * 400 kHz identification clock, and powers on the bus.
 *
 * @param Extension Per-slot miniport private data.
 * @param PhysicalBase Physical base address of the slot registers.
 * @param VirtualBase Kernel virtual mapping of the slot registers.
 * @param Length Length in bytes of the register mapping.
 * @param CrashdumpMode TRUE if operating in crashdump/hibernate context.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */

NTSTATUS
SdhcSlotInitialize(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PHYSICAL_ADDRESS PhysicalBase,
    _In_ PVOID VirtualBase,
    _In_ ULONG Length,
    _In_ BOOLEAN CrashdumpMode)
{
    NTSTATUS Status;
    USHORT Version;
    ULONG NormalIntMask;
    ULONG ErrorIntMask;
    UCHAR TimeoutClkField;

    UNREFERENCED_PARAMETER(PhysicalBase);

    Extension->BaseAddress = (PUCHAR)VirtualBase;
    Extension->BaseLength = Length;
    Extension->CrashdumpMode = CrashdumpMode;
    Extension->ConsecutiveErrorCount = 0;

    Extension->SavedIntStatusEnable = 0;
    Extension->SavedIntSignalEnable = 0;
    Extension->SavedTimeoutControl = 0;
    Extension->SavedPowerControl = 0;
    Extension->SavedClockControl = 0;
    Extension->SavedHostControl = 0;
    Extension->SavedHostControl2 = 0;
    Extension->TimeoutClockKhz = 0;
    Extension->Use64BitDma = FALSE;
    Extension->QuirkFlags = 0;
    Extension->SavedSpeedMode = SDHCI_HC2_UHS_SDR12;
    Extension->SavedClockKhz = 0;
    Extension->SavedBusWidth = 1;

    /* Read controller version */
    Version = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_VERSION);
    Extension->SpecVersion = (UCHAR)(Version & SDHCI_VERSION_MASK);

    /* Read capabilities */
    Extension->Capabilities = SDHCI_READ32(Extension->BaseAddress, SDHCI_CAPABILITIES);
    Extension->Capabilities2 = SDHCI_READ32(Extension->BaseAddress, SDHCI_CAPABILITIES2);

    if (Extension->SpecVersion >= SDHCI_SPEC_300)
    {
        Extension->BaseClockKhz = ((Extension->Capabilities & 0x0000FF00) >> 8) * 1000;
    }
    else
    {
        Extension->BaseClockKhz = ((Extension->Capabilities & 0x00003F00) >> 8) * 1000;
    }
    if (Extension->BaseClockKhz == 0)
    {
        Extension->BaseClockKhz = 200000;
    }

    if (Extension->SpecVersion >= SDHCI_SPEC_410)
    {
        USHORT Hc2;
        Hc2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);
        Hc2 |= SDHCI_HC2_HOST_VER4_ENABLE;
        if (Extension->Capabilities & SDHCI_CAP_64BIT_SYSTEM_BUS)
        {
            Hc2 |= SDHCI_HC2_ADDRESSING_64BIT;
            Extension->Use64BitDma = TRUE;
        }
        SdhcWriteHostControl2(Extension, Hc2);
    }

    SdhcLookupQuirks(Extension, 0, 0);

    TimeoutClkField = (UCHAR)(Extension->Capabilities & SDHCI_CAP_TIMEOUT_CLK_MASK);
    if (TimeoutClkField != 0)
    {
        if (Extension->Capabilities & SDHCI_CAP_TIMEOUT_CLK_MHZ)
        {
            Extension->TimeoutClockKhz = (ULONG)TimeoutClkField * 1000;
        }
        else
        {
            Extension->TimeoutClockKhz = (ULONG)TimeoutClkField;
        }
    }

    /* Perform full software reset */
    Status = SdhcResetHost(Extension, SDHCI_RESET_ALL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    NormalIntMask = SDHCI_INT_CMD_COMPLETE |
                    SDHCI_INT_XFER_COMPLETE |
                    SDHCI_INT_BLOCK_GAP |
                    SDHCI_INT_DMA |
                    SDHCI_INT_BUFFER_WRITE_READY |
                    SDHCI_INT_BUFFER_READ_READY |
                    SDHCI_INT_CARD_INSERTION |
                    SDHCI_INT_CARD_REMOVAL |
                    SDHCI_INT_CARD_INTERRUPT |
                    SDHCI_INT_RE_TUNING_EVENT |
                    SDHCI_INT_ERROR;
    ErrorIntMask = SDHCI_INT_CMD_TIMEOUT |
                   SDHCI_INT_CMD_CRC |
                   SDHCI_INT_CMD_END_BIT |
                   SDHCI_INT_CMD_INDEX |
                   SDHCI_INT_DATA_TIMEOUT |
                   SDHCI_INT_DATA_CRC |
                   SDHCI_INT_DATA_END_BIT |
                   SDHCI_INT_CURRENT_LIMIT |
                   SDHCI_INT_AUTO_CMD |
                   SDHCI_INT_ADMA |
                   SDHCI_INT_TUNING_ERROR;

    SdhcWriteIntStatusEnable(Extension, NormalIntMask | ErrorIntMask);
    SdhcWriteIntSignalEnable(Extension, NormalIntMask | ErrorIntMask);

    SdhcWriteTimeoutControl(Extension, 0x0E);

    /* Set initial clock to 400kHz for card detection */
    Status = SdhcSetClock(Extension, SD_INIT_CLOCK_KHZ);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Power on with 3.3V if supported, otherwise 3.0V, otherwise 1.8V */
    if (Extension->Capabilities & SDHCI_CAP_VOLTAGE_330)
    {
        Status = SdhcSetVoltage(Extension, SDHCI_PC_BUS_VOLTAGE_330);
    }
    else if (Extension->Capabilities & SDHCI_CAP_VOLTAGE_300)
    {
        Status = SdhcSetVoltage(Extension, SDHCI_PC_BUS_VOLTAGE_300);
    }
    else if (Extension->Capabilities & SDHCI_CAP_VOLTAGE_180)
    {
        Status = SdhcSetVoltage(Extension, SDHCI_PC_BUS_VOLTAGE_180);
    }
    else
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    return Status;
}

static NTSTATUS
SdhcExecuteTuningLoop(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR TuningCommand)
{
    USHORT Ctrl2;
    USHORT CmdReg;
    ULONG Timeout;
    ULONG Iteration;
    ULONG IntStatus;
    ULONG PresentState;
    ULONG i;

    Ctrl2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);
    Ctrl2 |= SDHCI_HC2_EXEC_TUNING;
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_HOST_CONTROL2, Ctrl2);

    for (Iteration = 0; Iteration < SDHC_TUNING_LOOP_COUNT; Iteration++)
    {
        for (Timeout = 0; Timeout < SDHC_CMD_TIMEOUT_US; Timeout += 10)
        {
            PresentState = SDHCI_READ32(Extension->BaseAddress, SDHCI_PRESENT_STATE);
            if (!(PresentState & (SDHCI_PS_CMD_INHIBIT | SDHCI_PS_DATA_INHIBIT)))
            {
                break;
            }
            KeStallExecutionProcessor(10);
        }
        if (PresentState & (SDHCI_PS_CMD_INHIBIT | SDHCI_PS_DATA_INHIBIT))
        {
            goto TuningFailedHardReset;
        }

        SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);

        SDHCI_WRITE16(Extension->BaseAddress, SDHCI_BLOCK_SIZE,
                      (USHORT)(SDHC_TUNING_BLOCK_SIZE & SDHCI_BLOCK_SIZE_MASK));
        SDHCI_WRITE16(Extension->BaseAddress, SDHCI_BLOCK_COUNT, 1);
        SDHCI_WRITE32(Extension->BaseAddress, SDHCI_ARGUMENT, 0);
        SDHCI_WRITE16(Extension->BaseAddress, SDHCI_TRANSFER_MODE,
                      SDHCI_TRNS_DATA_DIR_READ | SDHCI_TRNS_BLK_CNT_ENABLE);

        CmdReg = SDHCI_MAKE_CMD(TuningCommand,
                                SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                                SDHCI_CMD_INDEX_CHECK | SDHCI_CMD_DATA_PRESENT);
        SDHCI_WRITE16(Extension->BaseAddress, SDHCI_COMMAND, CmdReg);

        for (Timeout = 0; Timeout < SDHC_CMD_TIMEOUT_US; Timeout += 10)
        {
            IntStatus = SDHCI_READ32(Extension->BaseAddress, SDHCI_INT_STATUS);
            if (IntStatus & (SDHCI_INT_BUFFER_READ_READY | SDHCI_INT_ERROR))
            {
                break;
            }
            KeStallExecutionProcessor(10);
        }

        if (IntStatus & SDHCI_INT_ERROR)
        {
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS,
                          SDHCI_INT_ERROR_MASK | SDHCI_INT_ERROR);
            SdhcResetHost(Extension, SDHCI_RESET_CMD);
            SdhcResetHost(Extension, SDHCI_RESET_DATA);
        }
        else if (IntStatus & SDHCI_INT_BUFFER_READ_READY)
        {
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS,
                          SDHCI_INT_BUFFER_READ_READY);
            for (i = 0; i < SDHC_TUNING_BLOCK_SIZE / sizeof(ULONG); i++)
            {
                (VOID)SDHCI_READ32(Extension->BaseAddress, SDHCI_BUFFER_DATA_PORT);
            }
        }
        else
        {
            SdhcResetHost(Extension, SDHCI_RESET_CMD);
            SdhcResetHost(Extension, SDHCI_RESET_DATA);
        }

        SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS,
                      SDHCI_INT_XFER_COMPLETE | SDHCI_INT_CMD_COMPLETE);

        Ctrl2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);
        if (!(Ctrl2 & SDHCI_HC2_EXEC_TUNING))
        {
            if (Ctrl2 & SDHCI_HC2_SAMPLING_CLK_SELECT)
            {
                DPRINT1("SdhcExecuteTuningLoop: CMD%u converged after %lu iterations\n",
                        TuningCommand, Iteration + 1);
                return STATUS_SUCCESS;
            }

            DPRINT1("SdhcExecuteTuningLoop: CMD%u EXEC cleared without SAMPLING_CLK_SELECT\n",
                    TuningCommand);
            return STATUS_SD_TUNING_FAILED;
        }
    }

TuningFailedHardReset:
    Ctrl2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);
    Ctrl2 &= ~(SDHCI_HC2_EXEC_TUNING | SDHCI_HC2_SAMPLING_CLK_SELECT);
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_HOST_CONTROL2, Ctrl2);

    SdhcResetHost(Extension, SDHCI_RESET_CMD);
    SdhcResetHost(Extension, SDHCI_RESET_DATA);

    DPRINT1("SdhcExecuteTuningLoop: CMD%u failed to converge after %u iterations\n",
            TuningCommand, SDHC_TUNING_LOOP_COUNT);
    return STATUS_SD_TUNING_FAILED;
}

/**
 * @brief Dispatch a bus-level operation on the host controller.
 *
 * Handles reset, clock, voltage, bus width, speed mode, signaling
 * voltage, and tuning requests from the port driver.
 *
 * @param Extension Per-slot miniport private data.
 * @param Operation Describes the bus operation type and parameters.
 *
 * @return STATUS_SUCCESS on success, STATUS_NOT_SUPPORTED for unknown
 *         operations, or an NTSTATUS error code.
 */

NTSTATUS
SdhcSlotIssueBusOperation(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_BUS_OPERATION Operation)
{
    switch (Operation->Type)
    {
        case SdResetHost:
            return SdhcResetHost(Extension, SDHCI_RESET_ALL);

        case SdSetClock:
            return SdhcSetClock(Extension, Operation->Parameters.FrequencyKhz);

        case SdSetVoltage:
            return SdhcSetVoltage(Extension, Operation->Parameters.Voltage);

        case SdSetBusWidth:
            SdhcSetBusWidth(Extension, Operation->Parameters.BusWidth);
            return STATUS_SUCCESS;

        case SdSetBusSpeed:
            return SdhcSetBusSpeed(Extension, Operation->Parameters.SpeedMode);

        case SdSetSignalingVoltage:
        {
            USHORT Ctrl2;
            USHORT ClkCtrl;
            ULONG PresentState;
            BOOLEAN To18V = (Operation->Parameters.SignalingVoltage == 1);

            ClkCtrl = SDHCI_READ16(Extension->BaseAddress, SDHCI_CLOCK_CONTROL);
            ClkCtrl &= ~SDHCI_CLK_SD_CLK_ENABLE;
            SdhcWriteClockControl(Extension, ClkCtrl);

            if (To18V)
            {
                PresentState = SDHCI_READ32(Extension->BaseAddress, SDHCI_PRESENT_STATE);
                if (PresentState & (SDHCI_PS_DAT0_LEVEL | SDHCI_PS_DAT1_LEVEL |
                                    SDHCI_PS_DAT2_LEVEL | SDHCI_PS_DAT3_LEVEL))
                {
                    ClkCtrl |= SDHCI_CLK_SD_CLK_ENABLE;
                    SdhcWriteClockControl(Extension, ClkCtrl);
                    return STATUS_IO_DEVICE_ERROR;
                }
            }

            Ctrl2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);
            if (To18V)
            {
                Ctrl2 |= SDHCI_HC2_V18_SIGNAL_ENABLE;
            }
            else
            {
                Ctrl2 &= ~SDHCI_HC2_V18_SIGNAL_ENABLE;
            }
            SdhcWriteHostControl2(Extension, Ctrl2);

            KeStallExecutionProcessor(5000);

            if (To18V)
            {
                Ctrl2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);
                if (!(Ctrl2 & SDHCI_HC2_V18_SIGNAL_ENABLE))
                {
                    return STATUS_IO_DEVICE_ERROR;
                }
            }

            ClkCtrl = SDHCI_READ16(Extension->BaseAddress, SDHCI_CLOCK_CONTROL);
            ClkCtrl |= SDHCI_CLK_SD_CLK_ENABLE;
            SdhcWriteClockControl(Extension, ClkCtrl);

            if (To18V)
            {
                KeStallExecutionProcessor(1000);
                PresentState = SDHCI_READ32(Extension->BaseAddress, SDHCI_PRESENT_STATE);
                if ((PresentState & (SDHCI_PS_DAT0_LEVEL | SDHCI_PS_DAT1_LEVEL |
                                     SDHCI_PS_DAT2_LEVEL | SDHCI_PS_DAT3_LEVEL)) !=
                    (SDHCI_PS_DAT0_LEVEL | SDHCI_PS_DAT1_LEVEL |
                     SDHCI_PS_DAT2_LEVEL | SDHCI_PS_DAT3_LEVEL))
                {
                    return STATUS_IO_DEVICE_ERROR;
                }
            }

            return STATUS_SUCCESS;
        }

        case SdExecuteTuning:
        {
            UCHAR TuningCommand = Operation->Parameters.TuningCommand;

            if (TuningCommand != 19 && TuningCommand != 21)
            {
                USHORT Ctrl2;
                Ctrl2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);
                Ctrl2 |= SDHCI_HC2_EXEC_TUNING;
                SdhcWriteHostControl2(Extension, Ctrl2);
                return STATUS_SUCCESS;
            }

            return SdhcExecuteTuningLoop(Extension, TuningCommand);
        }

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/**
 * @brief Check whether a card is physically inserted in the slot.
 *
 * Reads the SDHCI Present State register card-inserted bit.
 *
 * @param Extension Per-slot miniport private data.
 *
 * @return TRUE if a card is inserted, FALSE otherwise.
 */

BOOLEAN
SdhcSlotGetCardDetectState(
    _In_ PSDHC_EXTENSION Extension)
{
    ULONG PresentState;

    PresentState = SDHCI_READ32(Extension->BaseAddress, SDHCI_PRESENT_STATE);
    return (PresentState & SDHCI_PS_CARD_INSERTED) ? TRUE : FALSE;
}

/**
 * @brief Process a pending interrupt from the host controller.
 *
 * Reads and acknowledges the interrupt status register, then
 * classifies events into normal completions, errors, card-change
 * notifications, SDIO interrupts, and re-tuning requests.
 *
 * @param Extension          Per-slot miniport private data.
 * @param Events             Receives the bitmask of normal interrupt events.
 * @param Errors             Receives the bitmask of error interrupt events.
 * @param NotifyCardChange   Set to TRUE if card insertion or removal detected.
 * @param NotifySdioInterrupt Set to TRUE if an SDIO card interrupt occurred.
 * @param NotifyTuning       Set to TRUE if re-tuning is required.
 *
 * @return TRUE if this controller raised the interrupt, FALSE otherwise.
 */

BOOLEAN
SdhcSlotInterrupt(
    _In_ PSDHC_EXTENSION Extension,
    _Out_ PULONG Events,
    _Out_ PULONG Errors,
    _Out_ PBOOLEAN NotifyCardChange,
    _Out_ PBOOLEAN NotifySdioInterrupt,
    _Out_ PBOOLEAN NotifyTuning)
{
    ULONG IntStatus;

    *Events = 0;
    *Errors = 0;
    *NotifyCardChange = FALSE;
    *NotifySdioInterrupt = FALSE;
    *NotifyTuning = FALSE;

    /* Read interrupt status (32-bit covers both normal and error) */
    IntStatus = SDHCI_READ32(Extension->BaseAddress, SDHCI_INT_STATUS);
    if (IntStatus == 0 || IntStatus == 0xFFFFFFFF)
    {
        return FALSE; /* Not our interrupt or controller removed */
    }

    /* Acknowledge all pending interrupts immediately */
    SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, IntStatus);

    *Events = IntStatus & SDHCI_INT_NORMAL_MASK;

    *Errors = (IntStatus & SDHCI_INT_ERROR_MASK) >> 16;

    /* Card insertion or removal */
    if (IntStatus & (SDHCI_INT_CARD_INSERTION | SDHCI_INT_CARD_REMOVAL))
    {
        *NotifyCardChange = TRUE;
    }

    /* SDIO card interrupt */
    if (IntStatus & SDHCI_INT_CARD_INTERRUPT)
    {
        *NotifySdioInterrupt = TRUE;
    }

    if (IntStatus & SDHCI_INT_RE_TUNING_EVENT)
    {
        *NotifyTuning = TRUE;
    }

    if (IntStatus & SDHCI_INT_TUNING_ERROR)
    {
        *NotifyTuning = TRUE;
    }

    return TRUE;
}

/**
 * @brief Submit an SD command/data request to the host controller.
 *
 * Delegates to SdhcSendCommand for synchronous command execution.
 *
 * @param Extension Per-slot miniport private data.
 * @param Request   The SD command request descriptor.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */

NTSTATUS
SdhcSlotIssueRequest(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    return SdhcSendCommand(Extension, Request);
}

/**
 * @brief Handle deferred processing for a completed SD request.
 *
 * Called at DPC level after the ISR signals completion. Maps hardware
 * error bits to NTSTATUS codes, reads the command response, and
 * processes PIO buffer-ready events.
 *
 * @param Extension Per-slot miniport private data.
 * @param Request   The SD command request being completed.
 * @param Events    Bitmask of normal interrupt events from the ISR.
 * @param Errors    Bitmask of error interrupt events from the ISR.
 */

VOID
SdhcSlotRequestDpc(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG Events,
    _In_ ULONG Errors)
{
    BOOLEAN HasData;
    BOOLEAN DataError;
    BOOLEAN RequestCompleted;

    HasData = (Request->Command.TransferType != SDTT_CMD_ONLY &&
               Request->Command.TransferType != SDTT_UNSPECIFIED);
    DataError = ((Errors & (SDHCI_INT_DATA_ERROR_MASK >> 16)) != 0);
    RequestCompleted = FALSE;

    if (Errors != 0)
    {
        if (Errors & (SDHCI_INT_CMD_TIMEOUT >> 16))
        {
            Request->Status = STATUS_IO_TIMEOUT;
        }
        else if (Errors & (SDHCI_INT_CMD_CRC >> 16))
        {
            Request->Status = STATUS_CRC_ERROR;
        }
        else if (Errors & ((SDHCI_INT_CMD_END_BIT | SDHCI_INT_CMD_INDEX) >> 16))
        {
            Request->Status = STATUS_IO_DEVICE_ERROR;
        }
        else if (Errors & (SDHCI_INT_DATA_TIMEOUT >> 16))
        {
            Request->Status = STATUS_IO_TIMEOUT;
        }
        else if (Errors & (SDHCI_INT_DATA_CRC >> 16))
        {
            Request->Status = STATUS_CRC_ERROR;
        }
        else if (Errors & (SDHCI_INT_DATA_END_BIT >> 16))
        {
            Request->Status = STATUS_IO_DEVICE_ERROR;
        }
        else if (Errors & (SDHCI_INT_CURRENT_LIMIT >> 16))
        {
            Request->Status = STATUS_IO_DEVICE_ERROR;
        }
        else if (Errors & (SDHCI_INT_AUTO_CMD >> 16))
        {
            Request->Status = STATUS_IO_DEVICE_ERROR;
        }
        else if (Errors & (SDHCI_INT_ADMA >> 16))
        {
            Request->Status = STATUS_ADAPTER_HARDWARE_ERROR;
        }
        else if (Errors & (SDHCI_INT_TUNING_ERROR >> 16))
        {
            Request->Status = STATUS_CRC_ERROR;
        }
        else
        {
            Request->Status = STATUS_IO_DEVICE_ERROR;
        }

        Extension->ConsecutiveErrorCount++;
        if (Extension->ConsecutiveErrorCount >= 3)
        {
            SdhcResetHost(Extension, SDHCI_RESET_ALL);
        }
        else
        {
            SdhcResetHost(Extension, SDHCI_RESET_CMD);
            if (DataError || Extension->ConsecutiveErrorCount >= 2)
            {
                SdhcResetHost(Extension, SDHCI_RESET_DATA);
            }
        }
        return;
    }

    /* Successful command completion — read response */
    if (Events & SDHCI_INT_CMD_COMPLETE)
    {
        SdhcReadResponse(Extension, Request);
        if (!HasData)
        {
            Request->Status = STATUS_SUCCESS;
            RequestCompleted = TRUE;
        }
    }

    /* Transfer complete */
    if (Events & SDHCI_INT_XFER_COMPLETE)
    {
        Request->Status = STATUS_SUCCESS;
        RequestCompleted = TRUE;
    }

    /* PIO buffer ready */
    if (Events & SDHCI_INT_BUFFER_READ_READY)
    {
        SdhcTransferDataPio(Extension, Request, TRUE);
    }

    if (Events & SDHCI_INT_BUFFER_WRITE_READY)
    {
        SdhcTransferDataPio(Extension, Request, FALSE);
    }

    if (RequestCompleted)
    {
        Extension->ConsecutiveErrorCount = 0;
    }
}

/**
 *
 *
 * @param Extension Per-slot miniport private data.
 *
 * @return STATUS_SUCCESS always.
 */

NTSTATUS
SdhcSlotSaveContext(
    _In_ PSDHC_EXTENSION Extension)
{
    Extension->SavedClockKhz = Extension->CurrentClockKhz;

    return STATUS_SUCCESS;
}

/**
 *
 *
 * @param Extension Per-slot miniport private data.
 *
 */

NTSTATUS
SdhcSlotRestoreContext(
    _In_ PSDHC_EXTENSION Extension)
{
    NTSTATUS Status;

    Status = SdhcResetHost(Extension, SDHCI_RESET_ALL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Extension->SpecVersion >= SDHCI_SPEC_410)
    {
        USHORT Hc2;
        Hc2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);
        Hc2 |= SDHCI_HC2_HOST_VER4_ENABLE;
        if (Extension->Use64BitDma)
        {
            Hc2 |= SDHCI_HC2_ADDRESSING_64BIT;
        }
        SdhcWriteHostControl2(Extension, Hc2);
    }

    if (Extension->SavedClockKhz != 0)
    {
        Status = SdhcSetClock(Extension, Extension->SavedClockKhz);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    SdhcSetBusWidth(Extension, Extension->SavedBusWidth);

    if (Extension->SavedSpeedMode != SDHCI_HC2_UHS_SDR12)
    {
        Status = SdhcSetBusSpeed(Extension, Extension->SavedSpeedMode);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Enable or disable interrupt signal delivery for specific events.
 *
 * Updates the SDHCI normal and error interrupt signal enable registers
 * according to the provided event bitmask.
 *
 * @param Extension Per-slot miniport private data.
 * @param EventMask Bitmask of SDHCI interrupt events to toggle.
 * @param Enable    TRUE to enable the events, FALSE to disable them.
 */

VOID
SdhcSlotToggleEvents(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG EventMask,
    _In_ BOOLEAN Enable)
{
    ULONG Signal;

    Signal = Extension->SavedIntSignalEnable;

    if (Enable)
    {
        Signal |= (EventMask & SDHCI_INT_NORMAL_MASK);
        Signal |= (EventMask & SDHCI_INT_ERROR_MASK);
    }
    else
    {
        Signal &= ~(EventMask & SDHCI_INT_NORMAL_MASK);
        Signal &= ~(EventMask & SDHCI_INT_ERROR_MASK);
    }

    SdhcWriteIntSignalEnable(Extension, Signal);
}

/**
 * @brief Acknowledge and clear pending interrupt events.
 *
 * Writes the event bitmask to the SDHCI interrupt status register
 * to clear the specified pending events.
 *
 * @param Extension Per-slot miniport private data.
 * @param EventMask Bitmask of SDHCI interrupt events to clear.
 */

VOID
SdhcSlotClearEvents(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG EventMask)
{
    SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, EventMask);
}

/**
 * @brief Shut down the host controller slot and release resources.
 *
 * Disables all interrupts, performs a full reset, powers off the bus,
 * and frees the ADMA2 descriptor buffer.
 *
 * @param Extension Per-slot miniport private data.
 */

VOID
SdhcSlotCleanup(
    _In_ PSDHC_EXTENSION Extension)
{
    SdhcWriteIntSignalEnable(Extension, 0);
    SdhcWriteIntStatusEnable(Extension, 0);

    /* Full reset */
    SdhcResetHost(Extension, SDHCI_RESET_ALL);

    /* Power off */
    SdhcWritePowerControl(Extension, 0);

    /* Free ADMA descriptors */
    if (Extension->AdmaDescriptorBuffer != NULL)
    {
        MmFreeContiguousMemory(Extension->AdmaDescriptorBuffer);
        Extension->AdmaDescriptorBuffer = NULL;
    }
}

/**
 * @brief Perform a software reset of the host controller.
 *
 * Writes the requested reset type to the SDHCI software reset register
 * and polls until the reset bit self-clears or a timeout is reached.
 *
 * @param Extension Per-slot miniport private data.
 * @param ResetType SDHCI reset bitmask (SDHCI_RESET_ALL, SDHCI_RESET_CMD,
 *                  or SDHCI_RESET_DATA).
 *
 * @return STATUS_SUCCESS if the reset completes, or STATUS_IO_TIMEOUT.
 */

NTSTATUS
SdhcResetHost(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR ResetType)
{
    ULONG Timeout;
    BOOLEAN ResetCompleted = FALSE;

    SDHCI_WRITE8(Extension->BaseAddress, SDHCI_SOFTWARE_RESET, ResetType);

    /* Wait for reset to complete (bit clears itself) */
    for (Timeout = 0; Timeout < SDHC_RESET_TIMEOUT_US; Timeout += 10)
    {
        if (!(SDHCI_READ8(Extension->BaseAddress, SDHCI_SOFTWARE_RESET) & ResetType))
        {
            ResetCompleted = TRUE;
            break;
        }
        KeStallExecutionProcessor(10);
    }

    if (!ResetCompleted)
    {
        return STATUS_IO_TIMEOUT;
    }

    if (ResetType & SDHCI_RESET_ALL)
    {
        SdhcApplyControllerState(Extension);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Configure the SD clock to the requested frequency.
 *
 * Disables the SD clock output, computes the appropriate divided-clock
 * divisor, programs the clock control register, waits for the internal
 * clock to stabilize, and re-enables the SD clock output.
 * Passing 0 disables the clock entirely.
 *
 * @param Extension   Per-slot miniport private data.
 * @param FrequencyKhz Target clock frequency in kHz, or 0 to disable.
 *
 * @return STATUS_SUCCESS on success, or STATUS_IO_TIMEOUT if the
 *         internal clock does not stabilize.
 */

NTSTATUS
SdhcSetClock(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG FrequencyKhz)
{
    USHORT ClkCtrl;
    ULONG Divisor;
    ULONG Timeout;
    ULONG ClockMultiplier;
    BOOLEAN UseProgrammableMode = FALSE;
    ULONG ActualFrequencyKhz;

    /* Disable SD clock first */
    ClkCtrl = SDHCI_READ16(Extension->BaseAddress, SDHCI_CLOCK_CONTROL);
    ClkCtrl &= ~SDHCI_CLK_SD_CLK_ENABLE;
    SdhcWriteClockControl(Extension, ClkCtrl);

    if (FrequencyKhz == 0)
    {
        Extension->CurrentClockKhz = 0;
        return STATUS_SUCCESS;
    }

    ClockMultiplier = (Extension->Capabilities2 & SDHCI_CAP2_CLK_MULTIPLIER_MASK) >>
                      SDHCI_CAP2_CLK_MULTIPLIER_SHIFT;
    if (Extension->SpecVersion >= SDHCI_SPEC_300 &&
        ClockMultiplier != 0 &&
        !(Extension->QuirkFlags & SDHC_QUIRK_NO_CLOCK_MULT))
    {
        ULONG NumeratorKhz = Extension->BaseClockKhz * (ClockMultiplier + 1);

        if (NumeratorKhz <= FrequencyKhz)
        {
            Divisor = 0;
        }
        else
        {
            Divisor = (NumeratorKhz + FrequencyKhz - 1) / FrequencyKhz;
            if (Divisor > 0)
            {
                Divisor -= 1;
            }
            if (Divisor > 0x3FF)
            {
                Divisor = 0x3FF;
            }
        }

        UseProgrammableMode = TRUE;
        ActualFrequencyKhz = NumeratorKhz / (Divisor + 1);
    }
    else
    {
        if (Extension->BaseClockKhz <= FrequencyKhz)
        {
            Divisor = 0;
        }
        else
        {
            ULONG MaxDivisor;

            MaxDivisor = (Extension->SpecVersion >= SDHCI_SPEC_300) ? 0x3FF : 0x80;

            Divisor = 1;
            while ((Extension->BaseClockKhz / (2 * Divisor)) > FrequencyKhz)
            {
                Divisor <<= 1;
                if (Divisor > MaxDivisor)
                {
                    Divisor = MaxDivisor;
                    break;
                }
            }
        }

        if (Divisor == 0)
        {
            ActualFrequencyKhz = Extension->BaseClockKhz;
        }
        else
        {
            ActualFrequencyKhz = Extension->BaseClockKhz / (2 * Divisor);
        }
    }

    ClkCtrl = (USHORT)((Divisor & 0xFF) << SDHCI_CLK_FREQ_SEL_SHIFT);
    ClkCtrl |= (USHORT)(((Divisor >> 8) & 0x03) << SDHCI_CLK_FREQ_SEL_UPPER_SHIFT);
    if (UseProgrammableMode)
    {
        ClkCtrl |= SDHCI_CLK_GENERATOR_SELECT;
    }
    ClkCtrl |= SDHCI_CLK_INT_CLK_ENABLE;

    SdhcWriteClockControl(Extension, ClkCtrl);

    /* Wait for internal clock stable */
    for (Timeout = 0; Timeout < SDHC_CLOCK_STABLE_TIMEOUT_US; Timeout += 10)
    {
        ClkCtrl = SDHCI_READ16(Extension->BaseAddress, SDHCI_CLOCK_CONTROL);
        if (ClkCtrl & SDHCI_CLK_INT_CLK_STABLE)
        {
            break;
        }
        KeStallExecutionProcessor(10);
    }

    if (!(ClkCtrl & SDHCI_CLK_INT_CLK_STABLE))
    {
        return STATUS_IO_TIMEOUT;
    }

    ClkCtrl |= SDHCI_CLK_SD_CLK_ENABLE;
    SdhcWriteClockControl(Extension, ClkCtrl);

    Extension->CurrentClockKhz = ActualFrequencyKhz;

    return STATUS_SUCCESS;
}

/**
 * @brief Set the bus voltage and power on the slot.
 *
 * Powers off the bus, programs the requested voltage level into the
 * SDHCI power control register, powers on, and verifies that the
 * power-on bit is set.
 *
 * @param Extension Per-slot miniport private data.
 * @param Voltage   SDHCI voltage selector (SDHCI_PC_BUS_VOLTAGE_330,
 *                  SDHCI_PC_BUS_VOLTAGE_300, or SDHCI_PC_BUS_VOLTAGE_180).
 *
 * @return STATUS_SUCCESS on success, or STATUS_UNSUCCESSFUL if power-on
 *         verification fails.
 */

NTSTATUS
SdhcSetVoltage(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR Voltage)
{
    /* Power off first */
    SdhcWritePowerControl(Extension, 0);
    KeStallExecutionProcessor(1000);

    /* Set voltage and power on */
    SdhcWritePowerControl(Extension, Voltage | SDHCI_PC_BUS_POWER_ON);
    KeStallExecutionProcessor(1000);

    if (Extension->QuirkFlags & SDHC_QUIRK_POWER_ON_DELAY)
    {
        KeStallExecutionProcessor(10000);
    }

    /* Verify power is on */
    if (!(SDHCI_READ8(Extension->BaseAddress, SDHCI_POWER_CONTROL) & SDHCI_PC_BUS_POWER_ON))
    {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Configure the data bus width of the host controller.
 *
 * Programs the SDHCI host control register for 1-bit, 4-bit,
 * or 8-bit data transfer width.
 *
 * @param Extension Per-slot miniport private data.
 * @param Width     Data bus width: 1, 4, or 8.
 */

VOID
SdhcSetBusWidth(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR Width)
{
    UCHAR HostCtrl;

    HostCtrl = SDHCI_READ8(Extension->BaseAddress, SDHCI_HOST_CONTROL);
    HostCtrl &= ~(SDHCI_HC_DATA_WIDTH_4BIT | SDHCI_HC_DATA_WIDTH_8BIT);

    switch (Width)
    {
        case 4:
            HostCtrl |= SDHCI_HC_DATA_WIDTH_4BIT;
            Extension->SavedBusWidth = 4;
            break;
        case 8:
            HostCtrl |= SDHCI_HC_DATA_WIDTH_8BIT;
            Extension->SavedBusWidth = 8;
            break;
        case 1:
        default:
            /* 1-bit mode: both bits cleared */
            Extension->SavedBusWidth = 1;
            break;
    }

    SdhcWriteHostControl(Extension, HostCtrl);
}

/**
 * @brief Configure the bus speed mode of the host controller.
 *
 * Programs the SDHCI host control and host control 2 registers for
 * the requested UHS speed mode (SDR12, SDR25, SDR50, SDR104, DDR50,
 * or HS400).
 *
 * @param Extension Per-slot miniport private data.
 * @param SpeedMode UHS mode selector (SDHCI_HC2_UHS_SDR12, etc.).
 *
 * @return STATUS_SUCCESS on success, or STATUS_NOT_SUPPORTED for
 *         unrecognized speed modes.
 */

NTSTATUS
SdhcSetBusSpeed(
    _In_ PSDHC_EXTENSION Extension,
    _In_ UCHAR SpeedMode)
{
    UCHAR HostCtrl;
    USHORT HostCtrl2;
    ULONG TargetClockKhz = 0;
    NTSTATUS Status;
    BOOLEAN SetHighSpeedBit = TRUE;

    HostCtrl = SDHCI_READ8(Extension->BaseAddress, SDHCI_HOST_CONTROL);
    HostCtrl2 = SDHCI_READ16(Extension->BaseAddress, SDHCI_HOST_CONTROL2);

    /* Clear speed-related bits */
    HostCtrl &= ~SDHCI_HC_HIGH_SPEED;
    HostCtrl2 &= ~SDHCI_HC2_UHS_MODE_MASK;

    switch (SpeedMode)
    {
        case SDHCI_HC2_UHS_SDR12:
            /* Default speed, no special bits */
            SetHighSpeedBit = FALSE;
            TargetClockKhz = SD_DEFAULT_SPEED_KHZ;
            break;

        case SDHCI_HC2_UHS_SDR25:
            HostCtrl2 |= SDHCI_HC2_UHS_SDR25;
            TargetClockKhz = SD_HIGH_SPEED_KHZ;
            break;

        case SDHCI_HC2_UHS_SDR50:
            HostCtrl2 |= SDHCI_HC2_UHS_SDR50;
            TargetClockKhz = SD_UHS_SDR50_KHZ;
            break;

        case SDHCI_HC2_UHS_SDR104:
            HostCtrl2 |= SDHCI_HC2_UHS_SDR104;
            TargetClockKhz = SD_UHS_SDR104_KHZ;
            break;

        case SDHCI_HC2_UHS_DDR50:
            HostCtrl2 |= SDHCI_HC2_UHS_DDR50;
            TargetClockKhz = SD_UHS_DDR50_KHZ;
            break;

        case SDHCI_HC2_UHS_HS400:
            HostCtrl2 |= SDHCI_HC2_UHS_HS400;
            TargetClockKhz = MMC_HS400_KHZ;
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    if (SetHighSpeedBit && !(Extension->QuirkFlags & SDHC_QUIRK_NO_HIGHSPEED_BIT))
    {
        HostCtrl |= SDHCI_HC_HIGH_SPEED;
    }

    SdhcWriteHostControl(Extension, HostCtrl);
    SdhcWriteHostControl2(Extension, HostCtrl2);

    Status = SdhcSetClock(Extension, TargetClockKhz);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Extension->SavedSpeedMode = SpeedMode;


    return STATUS_SUCCESS;
}

static
NTSTATUS
SdhcWaitForBusyRelease(
    _In_ PSDHC_EXTENSION Extension)
{
    ULONG PresentState = 0;
    ULONG Timeout;

    for (Timeout = 0; Timeout < SDHC_CMD_TIMEOUT_US; Timeout += 10)
    {
        PresentState = SDHCI_READ32(Extension->BaseAddress, SDHCI_PRESENT_STATE);
        if (!(PresentState & SDHCI_PS_DATA_INHIBIT))
        {
            return STATUS_SUCCESS;
        }

        KeStallExecutionProcessor(10);
    }

    if (!(PresentState & SDHCI_PS_DAT0_LEVEL))
    {
        SdhcResetHost(Extension, SDHCI_RESET_DATA);
    }

    return STATUS_IO_TIMEOUT;
}

/**
 * @brief Send an SD command and optionally perform a data transfer.
 *
 * Waits for the CMD (and optionally DAT) lines to become free,
 * programs the argument, transfer mode, and command registers,
 * polls for command completion, reads the response, and for data
 * commands polls for transfer completion handling PIO and DMA events.
 *
 * @param Extension Per-slot miniport private data.
 * @param Request   The SD command request descriptor including command
 *                  index, argument, response type, and data parameters.
 *
 * @return STATUS_SUCCESS on success, STATUS_IO_TIMEOUT on timeout, or
 *         STATUS_IO_DEVICE_ERROR on hardware error.
 */

NTSTATUS
SdhcSendCommand(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    USHORT CmdReg;
    USHORT TransferMode = 0;
    ULONG PresentState;
    ULONG Timeout;
    ULONG IntStatus;
    USHORT CmdFlags = 0;
    BOOLEAN HasData;
    NTSTATUS Status;

    HasData = (Request->Command.TransferType != SDTT_CMD_ONLY &&
               Request->Command.TransferType != SDTT_UNSPECIFIED);
    Request->BytesTransferred = 0;

    /* Wait for CMD line not inhibited */
    for (Timeout = 0; Timeout < SDHC_CMD_TIMEOUT_US; Timeout += 10)
    {
        PresentState = SDHCI_READ32(Extension->BaseAddress, SDHCI_PRESENT_STATE);
        if (!(PresentState & SDHCI_PS_CMD_INHIBIT))
        {
            break;
        }
        KeStallExecutionProcessor(10);
    }

    if (PresentState & SDHCI_PS_CMD_INHIBIT)
    {
        SdhcResetHost(Extension, SDHCI_RESET_CMD);
        return STATUS_IO_TIMEOUT;
    }

    /* If data transfer, also wait for DAT line */
    if (HasData)
    {
        for (Timeout = 0; Timeout < SDHC_CMD_TIMEOUT_US; Timeout += 10)
        {
            PresentState = SDHCI_READ32(Extension->BaseAddress, SDHCI_PRESENT_STATE);
            if (!(PresentState & SDHCI_PS_DATA_INHIBIT))
            {
                break;
            }
            KeStallExecutionProcessor(10);
        }

        if (PresentState & SDHCI_PS_DATA_INHIBIT)
        {
            SdhcResetHost(Extension, SDHCI_RESET_DATA);
            return STATUS_IO_TIMEOUT;
        }
    }

    /* Clear all pending interrupts */
    SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);

    /* Set up data transfer if needed */
    if (HasData)
    {
        /* Try ADMA2 first, fall back to PIO */
        if (Request->UseAdma && (Extension->Capabilities & SDHCI_CAP_ADMA2_SUPPORT))
        {
            UCHAR HostCtrl;

            Request->AdmaDescriptorTable = NULL;
            Request->AdmaDescriptorPhysical.QuadPart = 0;

            Status = SdhcSetupAdma2Transfer(Extension, Request);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }

            HostCtrl = SDHCI_READ8(Extension->BaseAddress, SDHCI_HOST_CONTROL);
            HostCtrl &= ~SDHCI_HC_DMA_SELECT_MASK;
            if (Extension->Use64BitDma)
            {
                HostCtrl |= SDHCI_HC_DMA_SELECT_ADMA2_64;
            }
            else
            {
                HostCtrl |= SDHCI_HC_DMA_SELECT_ADMA2;
            }
            SdhcWriteHostControl(Extension, HostCtrl);

            TransferMode |= SDHCI_TRNS_DMA_ENABLE;
        }

        /* Program block size and count */
        SDHCI_WRITE16(Extension->BaseAddress, SDHCI_BLOCK_SIZE,
                      (USHORT)(Request->BlockSize & SDHCI_BLOCK_SIZE_MASK));
        SDHCI_WRITE16(Extension->BaseAddress, SDHCI_BLOCK_COUNT,
                      (USHORT)Request->BlockCount);

        /* Transfer mode flags */
        if (Request->Command.TransferDirection == SDTD_READ)
        {
            TransferMode |= SDHCI_TRNS_DATA_DIR_READ;
        }

        if (Request->Command.TransferType == SDTT_MULTI_BLOCK)
        {
            TransferMode |= SDHCI_TRNS_MULTI_BLOCK | SDHCI_TRNS_BLK_CNT_ENABLE |
                            SDHCI_TRNS_AUTO_CMD12;
        }
        else if (Request->Command.TransferType == SDTT_MULTI_BLOCK_NO_CMD12)
        {
            TransferMode |= SDHCI_TRNS_MULTI_BLOCK | SDHCI_TRNS_BLK_CNT_ENABLE;
        }
        else
        {
            TransferMode |= SDHCI_TRNS_BLK_CNT_ENABLE;
        }
    }

    /* Write argument register */
    SDHCI_WRITE32(Extension->BaseAddress, SDHCI_ARGUMENT, Request->Argument);

    /* Write transfer mode (before command for data transfers) */
    if (HasData)
    {
        SDHCI_WRITE16(Extension->BaseAddress, SDHCI_TRANSFER_MODE, TransferMode);
    }

    /* Build command register value */
    switch (Request->Command.ResponseType)
    {
        case SDRT_NONE:
            CmdFlags = SDHCI_CMD_RESP_NONE;
            break;
        case SDRT_1:
        case SDRT_5:
        case SDRT_6:
            CmdFlags = SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK;
            break;
        case SDRT_1B:
        case SDRT_5B:
            CmdFlags = SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK;
            break;
        case SDRT_2:
            CmdFlags = SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC_CHECK;
            break;
        case SDRT_3:
        case SDRT_4:
            CmdFlags = SDHCI_CMD_RESP_48;
            break;
        default:
            CmdFlags = SDHCI_CMD_RESP_NONE;
            break;
    }

    if (HasData)
    {
        CmdFlags |= SDHCI_CMD_DATA_PRESENT;
    }

    CmdReg = SDHCI_MAKE_CMD(Request->Command.Cmd, CmdFlags);

    /* Issue the command — write to COMMAND register triggers execution */
    SDHCI_WRITE16(Extension->BaseAddress, SDHCI_COMMAND, CmdReg);

    /* Poll for command completion (for synchronous path) */
    for (Timeout = 0; Timeout < SDHC_CMD_TIMEOUT_US; Timeout += 10)
    {
        IntStatus = SDHCI_READ32(Extension->BaseAddress, SDHCI_INT_STATUS);

        if (IntStatus & SDHCI_INT_CMD_COMPLETE)
        {
            /* Acknowledge command complete */
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, SDHCI_INT_CMD_COMPLETE);
            break;
        }

        if (IntStatus & SDHCI_INT_ERROR)
        {
            /* Acknowledge and handle error */
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, IntStatus);

            if (IntStatus & SDHCI_INT_CMD_TIMEOUT)
            {
                SdhcResetHost(Extension, SDHCI_RESET_CMD);
                Request->Status = STATUS_IO_TIMEOUT;
                return STATUS_IO_TIMEOUT;
            }

            SdhcResetHost(Extension, SDHCI_RESET_CMD);
            SdhcResetHost(Extension, SDHCI_RESET_DATA);
            Request->Status = STATUS_IO_DEVICE_ERROR;
            return STATUS_IO_DEVICE_ERROR;
        }

        KeStallExecutionProcessor(10);
    }

    if (Timeout >= SDHC_CMD_TIMEOUT_US)
    {
        SdhcResetHost(Extension, SDHCI_RESET_CMD);
        Request->Status = STATUS_IO_TIMEOUT;
        return STATUS_IO_TIMEOUT;
    }

    /* Read response */
    SdhcReadResponse(Extension, Request);

    /* If no data, we're done */
    if (!HasData)
    {
        if (Request->Command.ResponseType == SDRT_1B ||
            Request->Command.ResponseType == SDRT_5B)
        {
            Status = SdhcWaitForBusyRelease(Extension);
            if (!NT_SUCCESS(Status))
            {
                Request->Status = Status;
                return Status;
            }
        }

        Request->Status = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    /* Wait for transfer complete (for synchronous DMA path) */
    for (Timeout = 0; Timeout < SDHC_CMD_TIMEOUT_US * 5; Timeout += 10)
    {
        IntStatus = SDHCI_READ32(Extension->BaseAddress, SDHCI_INT_STATUS);

        if (IntStatus & SDHCI_INT_XFER_COMPLETE)
        {
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, SDHCI_INT_XFER_COMPLETE);
            Request->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }

        /* Handle PIO buffer ready (when DMA not used) */
        if (IntStatus & SDHCI_INT_BUFFER_READ_READY)
        {
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, SDHCI_INT_BUFFER_READ_READY);
            SdhcTransferDataPio(Extension, Request, TRUE);
        }

        if (IntStatus & SDHCI_INT_BUFFER_WRITE_READY)
        {
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, SDHCI_INT_BUFFER_WRITE_READY);
            SdhcTransferDataPio(Extension, Request, FALSE);
        }

        if (IntStatus & SDHCI_INT_ERROR)
        {
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, IntStatus);
            SdhcResetHost(Extension, SDHCI_RESET_CMD);
            SdhcResetHost(Extension, SDHCI_RESET_DATA);
            Request->Status = STATUS_IO_DEVICE_ERROR;
            return STATUS_IO_DEVICE_ERROR;
        }

        /* SDMA boundary interrupt — reprogram address */
        if (IntStatus & SDHCI_INT_DMA)
        {
            ULONG SdmaAddr;
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_INT_STATUS, SDHCI_INT_DMA);
            SdmaAddr = SDHCI_READ32(Extension->BaseAddress, SDHCI_SDMA_ADDRESS);
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_SDMA_ADDRESS, SdmaAddr);
        }

        KeStallExecutionProcessor(10);
    }

    SdhcResetHost(Extension, SDHCI_RESET_CMD);
    SdhcResetHost(Extension, SDHCI_RESET_DATA);
    Request->Status = STATUS_IO_TIMEOUT;
    return STATUS_IO_TIMEOUT;
}

/**
 * @brief Read the command response from the host controller registers.
 *
 * For R2 (136-bit) responses the four response registers are read and
 * bit-shifted to reconstruct the full CID/CSD payload. For 48-bit
 * responses only the first register is read.
 *
 * @param Extension Per-slot miniport private data.
 * @param Request   The SD command request whose Response array is filled.
 */

VOID
SdhcReadResponse(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (Request->Command.ResponseType == SDRT_NONE)
    {
        return;
    }

    if (Request->Command.ResponseType == SDRT_2)
    {
        /* 136-bit response: R2 (CID, CSD)
         * SDHCI stores response with bits [127:8] in registers 0x10-0x1F
         * Shift to match card format */
        Request->Response[0] = (SDHCI_READ32(Extension->BaseAddress, SDHCI_RESPONSE0) << 8);
        Request->Response[1] = (SDHCI_READ32(Extension->BaseAddress, SDHCI_RESPONSE1) << 8) |
                               (SDHCI_READ32(Extension->BaseAddress, SDHCI_RESPONSE0) >> 24);
        Request->Response[2] = (SDHCI_READ32(Extension->BaseAddress, SDHCI_RESPONSE2) << 8) |
                               (SDHCI_READ32(Extension->BaseAddress, SDHCI_RESPONSE1) >> 24);
        Request->Response[3] = (SDHCI_READ32(Extension->BaseAddress, SDHCI_RESPONSE3) << 8) |
                               (SDHCI_READ32(Extension->BaseAddress, SDHCI_RESPONSE2) >> 24);
    }
    else
    {
        /* 48-bit response: R1, R1b, R3, R4, R5, R6, R7 */
        Request->Response[0] = SDHCI_READ32(Extension->BaseAddress, SDHCI_RESPONSE0);
        Request->Response[1] = 0;
        Request->Response[2] = 0;
        Request->Response[3] = 0;
    }
}

static
USHORT
SdhcEncodeAdma2Length(
    _In_ ULONG ChunkSize)
{
    if (ChunkSize == SDHCI_ADMA2_MAX_LENGTH)
    {
        return 0;
    }
    return (USHORT)(ChunkSize & 0xFFFF);
}

/**
 * @brief Build the ADMA2 descriptor table for a DMA data transfer.
 *
 * Allocates a contiguous descriptor buffer on first use, then builds
 *
 * @param Extension Per-slot miniport private data.
 * @param Request   The SD command request containing the data buffer,
 *                  block size, and block count.
 *
 * @return STATUS_SUCCESS on success, STATUS_INVALID_PARAMETER if no
 *         data buffer is provided, STATUS_INSUFFICIENT_RESOURCES on
 *         allocation failure, or STATUS_BUFFER_TOO_SMALL if the
 *         transfer exceeds the maximum descriptor count.
 */

NTSTATUS
SdhcSetupAdma2Transfer(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    PSDHCI_ADMA2_DESCRIPTOR_32 Desc32;
    PSDHCI_ADMA2_DESCRIPTOR_64 Desc64;
    PHYSICAL_ADDRESS NextPhysAddr;
    PHYSICAL_ADDRESS PhysAddr;
    PHYSICAL_ADDRESS HighAddr;
    ULONG TotalLength;
    ULONG Remaining;
    ULONG ChunkSize;
    ULONG PageRemaining;
    PUCHAR Buffer;
    ULONG Index;
    ULONG DescStride;
    BOOLEAN Use64Bit;

    if (Request->DataBuffer == NULL && Request->DataMdl == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    TotalLength = Request->BlockSize * Request->BlockCount;

    Use64Bit = Extension->Use64BitDma;
    DescStride = Use64Bit ? sizeof(SDHCI_ADMA2_DESCRIPTOR_64)
                          : sizeof(SDHCI_ADMA2_DESCRIPTOR_32);

    if (Extension->AdmaDescriptorBuffer == NULL)
    {
        ULONG DescSize = SDHC_MAX_ADMA2_DESCRIPTORS * sizeof(SDHCI_ADMA2_DESCRIPTOR_64);
        HighAddr.QuadPart = Use64Bit ? 0xFFFFFFFFFFFFFFFFULL : 0xFFFFFFFFULL;

        Extension->AdmaDescriptorBuffer = MmAllocateContiguousMemory(DescSize, HighAddr);
        if (Extension->AdmaDescriptorBuffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Extension->AdmaDescriptorPhysical = MmGetPhysicalAddress(Extension->AdmaDescriptorBuffer);
        Extension->AdmaDescriptorSize = DescSize;
    }

    RtlZeroMemory(Extension->AdmaDescriptorBuffer, Extension->AdmaDescriptorSize);
    Desc32 = (PSDHCI_ADMA2_DESCRIPTOR_32)Extension->AdmaDescriptorBuffer;
    Desc64 = (PSDHCI_ADMA2_DESCRIPTOR_64)Extension->AdmaDescriptorBuffer;

    /* Build descriptor chain from data buffer */
    if (Request->DataBuffer != NULL)
    {
        Buffer = (PUCHAR)Request->DataBuffer;
    }
    else
    {
        Buffer = (PUCHAR)MmGetSystemAddressForMdlSafe(Request->DataMdl, NormalPagePriority);
        if (Buffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    Remaining = TotalLength;
    Index = 0;

    while (Remaining > 0 && Index < SDHC_MAX_ADMA2_DESCRIPTORS)
    {
        PhysAddr = MmGetPhysicalAddress(Buffer);
        PageRemaining = PAGE_SIZE - (PhysAddr.LowPart & (PAGE_SIZE - 1));
        ChunkSize = (Remaining < PageRemaining) ? Remaining : PageRemaining;

        while (ChunkSize < Remaining &&
               ChunkSize < SDHCI_ADMA2_MAX_LENGTH)
        {
            ULONG ExtensionSize;

            NextPhysAddr = MmGetPhysicalAddress(Buffer + ChunkSize);
            if (NextPhysAddr.QuadPart != (PhysAddr.QuadPart + ChunkSize))
            {
                break;
            }

            ExtensionSize = PAGE_SIZE;
            if (ChunkSize + ExtensionSize > Remaining)
            {
                ExtensionSize = Remaining - ChunkSize;
            }
            if (ChunkSize + ExtensionSize > SDHCI_ADMA2_MAX_LENGTH)
            {
                ExtensionSize = SDHCI_ADMA2_MAX_LENGTH - ChunkSize;
            }

            if (ExtensionSize == 0)
            {
                break;
            }

            ChunkSize += ExtensionSize;
        }

        if (ChunkSize == 0)
        {
            return STATUS_UNSUCCESSFUL;
        }
        if (!Use64Bit && PhysAddr.HighPart != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (Use64Bit)
        {
            Desc64[Index].Attributes = SDHCI_ADMA2_VALID | SDHCI_ADMA2_ACT_TRAN;
            Desc64[Index].Length = SdhcEncodeAdma2Length(ChunkSize);
            Desc64[Index].AddressLow = PhysAddr.LowPart;
            Desc64[Index].AddressHigh = (ULONG)PhysAddr.HighPart;
        }
        else
        {
            Desc32[Index].Attributes = SDHCI_ADMA2_VALID | SDHCI_ADMA2_ACT_TRAN;
            Desc32[Index].Length = SdhcEncodeAdma2Length(ChunkSize);
            Desc32[Index].Address = PhysAddr.LowPart;
        }

        Buffer += ChunkSize;
        Remaining -= ChunkSize;
        Index++;
    }

    if (Remaining > 0)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Index > 0)
    {
        if (Use64Bit)
        {
            Desc64[Index - 1].Attributes |= SDHCI_ADMA2_END;
        }
        else
        {
            Desc32[Index - 1].Attributes |= SDHCI_ADMA2_END;
        }
    }

    SDHCI_WRITE32(Extension->BaseAddress, SDHCI_ADMA_ADDRESS_LOW,
                  Extension->AdmaDescriptorPhysical.LowPart);
    if (Use64Bit)
    {
        SDHCI_WRITE32(Extension->BaseAddress, SDHCI_ADMA_ADDRESS_HIGH,
                      (ULONG)Extension->AdmaDescriptorPhysical.HighPart);
    }
    else
    {
        SDHCI_WRITE32(Extension->BaseAddress, SDHCI_ADMA_ADDRESS_HIGH, 0);
    }

    UNREFERENCED_PARAMETER(DescStride);

    Request->AdmaDescriptorTable = Extension->AdmaDescriptorBuffer;
    Request->AdmaDescriptorPhysical = Extension->AdmaDescriptorPhysical;

    return STATUS_SUCCESS;
}

/**
 * @brief Transfer one block of data via programmed I/O.
 *
 * Reads or writes BlockSize bytes through the SDHCI buffer data port
 * register one DWORD at a time.
 *
 * @param Extension Per-slot miniport private data.
 * @param Request   The SD command request containing the data buffer
 *                  and block size.
 * @param IsRead    TRUE to read from the controller into the buffer,
 *                  FALSE to write from the buffer to the controller.
 *
 * @return STATUS_SUCCESS on success, or STATUS_INVALID_PARAMETER if
 *         no data buffer is provided.
 */

NTSTATUS
SdhcTransferDataPio(
    _In_ PSDHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ BOOLEAN IsRead)
{
    PUCHAR BaseBuffer;
    PULONG Buffer;
    ULONG TotalLength;
    ULONG Offset;
    ULONG Words;
    ULONG i;

    if (Request->DataBuffer == NULL && Request->DataMdl == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->DataMdl != NULL)
    {
        BaseBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(Request->DataMdl,
                                                          NormalPagePriority);
        if (BaseBuffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        BaseBuffer = (PUCHAR)Request->DataBuffer;
    }

    TotalLength = Request->BlockSize * Request->BlockCount;
    Offset = Request->BytesTransferred;
    if (Offset + Request->BlockSize > TotalLength)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Buffer = (PULONG)(BaseBuffer + Offset);
    Words = Request->BlockSize / sizeof(ULONG);

    if (IsRead)
    {
        for (i = 0; i < Words; i++)
        {
            Buffer[i] = SDHCI_READ32(Extension->BaseAddress, SDHCI_BUFFER_DATA_PORT);
        }
    }
    else
    {
        for (i = 0; i < Words; i++)
        {
            SDHCI_WRITE32(Extension->BaseAddress, SDHCI_BUFFER_DATA_PORT, Buffer[i]);
        }
    }

    Request->BytesTransferred += Request->BlockSize;
    return STATUS_SUCCESS;
}

NTSTATUS
SdhcSlotSetTimeoutFromCsd(
    _In_ PSDHC_EXTENSION Extension,
    _In_ ULONG TaacNs,
    _In_ ULONG Nsac)
{
    ULONG TmClkKhz;
    ULONGLONG TaacCycles;
    ULONGLONG NsacCycles;
    ULONGLONG RequiredCycles;
    ULONGLONG Window;
    UCHAR Tocnt;

    TmClkKhz = Extension->TimeoutClockKhz;
    if (TmClkKhz == 0)
    {
        TmClkKhz = Extension->CurrentClockKhz;
    }

    if (TmClkKhz == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    TaacCycles = ((ULONGLONG)TaacNs * (ULONGLONG)TmClkKhz) / 1000000ULL;

    if (Extension->CurrentClockKhz != 0)
    {
        NsacCycles = ((ULONGLONG)Nsac * 100ULL * (ULONGLONG)TmClkKhz) /
                     (ULONGLONG)Extension->CurrentClockKhz;
    }
    else
    {
        NsacCycles = 0;
    }

    RequiredCycles = (TaacCycles + NsacCycles) * 100ULL;

    if (RequiredCycles < (ULONGLONG)TmClkKhz * 100ULL)
    {
        RequiredCycles = (ULONGLONG)TmClkKhz * 100ULL;
    }

    Tocnt = 0;
    Window = 1ULL << 13;
    while (Tocnt < 0x0E && Window < RequiredCycles)
    {
        Window <<= 1;
        Tocnt++;
    }

    SdhcWriteTimeoutControl(Extension, Tocnt);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SdhcIssueRequestFromBus(
    _In_ PVOID Context,
    _In_ PVOID Request)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Request);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
SdhcRegisterBusCallbacks(
    _In_ PVOID FdoExt,
    _In_ PVOID MiniportExt,
    _In_ PVOID IssueRequestFunc)
{
    if (FdoExt == NULL || IssueRequestFunc == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    UNREFERENCED_PARAMETER(MiniportExt);

    return STATUS_SUCCESS;
}

/**
 * @brief Entry point for the SDHCI miniport driver.
 *
 * Fills an SDPORT_INITIALIZATION_DATA structure with callback pointers
 * and calls SdPortInitialize() to register with the SD port driver.
 *
 * @param DriverObject  The driver object created by the I/O manager.
 * @param RegistryPath  Registry path to the driver's service key.
 *
 * @return NTSTATUS from SdPortInitialize.
 */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    SDPORT_INITIALIZATION_DATA InitData;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.StructureSize = sizeof(InitData);
    InitData.GetSlotCount = (PSDPORT_GET_SLOT_COUNT)SdhcGetSlotCount;
    InitData.GetSlotCapabilities = (PSDPORT_GET_SLOT_CAPABILITIES)SdhcGetSlotCapabilities;
    InitData.Initialize = (PSDPORT_INITIALIZE)SdhcSlotInitialize;
    InitData.IssueBusOperation = (PSDPORT_ISSUE_BUS_OPERATION)SdhcSlotIssueBusOperation;
    InitData.GetCardDetectState = (PSDPORT_GET_CARD_DETECT_STATE)SdhcSlotGetCardDetectState;
    InitData.Interrupt = (PSDPORT_INTERRUPT)SdhcSlotInterrupt;
    InitData.IssueRequest = (PSDPORT_ISSUE_REQUEST)SdhcSlotIssueRequest;
    InitData.RequestDpc = (PSDPORT_REQUEST_DPC)SdhcSlotRequestDpc;
    InitData.SaveContext = (PSDPORT_SAVE_CONTEXT)SdhcSlotSaveContext;
    InitData.RestoreContext = (PSDPORT_RESTORE_CONTEXT)SdhcSlotRestoreContext;
    InitData.ToggleEvents = (PSDPORT_TOGGLE_EVENTS)SdhcSlotToggleEvents;
    InitData.ClearEvents = (PSDPORT_CLEAR_EVENTS)SdhcSlotClearEvents;
    InitData.Cleanup = (PSDPORT_CLEANUP)SdhcSlotCleanup;
    InitData.PrivateExtensionSize = sizeof(SDHC_EXTENSION);

    return SdPortInitialize(DriverObject, RegistryPath, &InitData);
}
