/*
 * PROJECT:     ReactOS SD Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Power, clock, and bus management for SD/eMMC host controllers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include "sdport.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/**
 * @brief Set the bus voltage for a slot via the miniport.
 *
 * Issues a SdSetVoltage bus operation to the miniport. The miniport is
 * responsible for programming the SDHCI power control register and
 * waiting for the voltage to stabilize.
 *
 * @param[in] FdoExtension   Pointer to the FDO device extension.
 * @param[in] SlotExtension  Pointer to the slot extension (reserved for future use).
 * @param[in] Voltage        SDHCI power control register value
 *                           (e.g., SDHCI_PC_BUS_VOLTAGE_330).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code from the
 *         miniport bus operation.
 */
NTSTATUS
SdPortSetSlotPower(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ UCHAR Voltage)
{
    SDPORT_BUS_OPERATION BusOp;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(SlotExtension);

    DPRINT1("SdPortSetSlotPower: Voltage=0x%02x\n", Voltage);

    if (FdoExtension->MiniportInitData.IssueBusOperation == NULL)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&BusOp, sizeof(BusOp));
    BusOp.Type = SdSetVoltage;
    BusOp.Parameters.Voltage = Voltage;

    Status = FdoExtension->MiniportInitData.IssueBusOperation(
                FdoExtension->MiniportPrivateExtension,
                &BusOp);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortSetSlotPower: IssueBusOperation failed (0x%08lx)\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Set the SD bus clock frequency for a slot via the miniport.
 *
 * Issues a SdSetClock bus operation to the miniport. The miniport is
 * responsible for calculating the closest achievable clock divider and
 * programming the SDHCI clock control register.
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in,out] SlotExtension  Pointer to the slot extension; CurrentClockKhz
 *                               is updated on success.
 * @param[in]     FrequencyKhz   Desired clock frequency in kilohertz.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code from the
 *         miniport bus operation.
 */
NTSTATUS
SdPortSetSlotClock(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ ULONG FrequencyKhz)
{
    SDPORT_BUS_OPERATION BusOp;
    NTSTATUS Status;

    DPRINT1("SdPortSetSlotClock: FrequencyKhz=%lu\n", FrequencyKhz);

    if (FdoExtension->MiniportInitData.IssueBusOperation == NULL)
    {
        SlotExtension->CurrentClockKhz = FrequencyKhz;
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&BusOp, sizeof(BusOp));
    BusOp.Type = SdSetClock;
    BusOp.Parameters.FrequencyKhz = FrequencyKhz;

    Status = FdoExtension->MiniportInitData.IssueBusOperation(
                FdoExtension->MiniportPrivateExtension,
                &BusOp);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortSetSlotClock: IssueBusOperation failed (0x%08lx)\n", Status);
        return Status;
    }

    SlotExtension->CurrentClockKhz = FrequencyKhz;
    return STATUS_SUCCESS;
}

/**
 * @brief Issue a full host controller reset via the miniport.
 *
 * Sends a SdResetHost bus operation to the miniport. This resets all
 * SDHCI registers to their default values and aborts any in-progress
 * transfers.
 *
 * @param[in] FdoExtension   Pointer to the FDO device extension.
 * @param[in] SlotExtension  Pointer to the slot extension (reserved for future use).
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code from the
 *         miniport bus operation.
 */
NTSTATUS
SdPortResetHost(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension)
{
    SDPORT_BUS_OPERATION BusOp;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(SlotExtension);

    DPRINT1("SdPortResetHost\n");

    if (FdoExtension->MiniportInitData.IssueBusOperation == NULL)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&BusOp, sizeof(BusOp));
    BusOp.Type = SdResetHost;

    Status = FdoExtension->MiniportInitData.IssueBusOperation(
                FdoExtension->MiniportPrivateExtension,
                &BusOp);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortResetHost: IssueBusOperation failed (0x%08lx)\n", Status);
    }

    return Status;
}

/**
 * @brief Change the data bus width for a slot via the miniport.
 *
 * Validates the requested width and 8-bit capability, then issues a
 * SdSetBusWidth bus operation to the miniport. The miniport programs
 * the SDHCI Host Control register to reflect the new bus width.
 *
 * @param[in]     FdoExtension   Pointer to the FDO device extension.
 * @param[in,out] SlotExtension  Pointer to the slot extension; CurrentBusWidth
 *                               is updated on success.
 * @param[in]     BusWidth       Desired bus width (1, 4, or 8 bits).
 *
 * @return STATUS_SUCCESS on success, STATUS_INVALID_PARAMETER for invalid
 *         width values, STATUS_NOT_SUPPORTED if 8-bit is requested but not
 *         supported, or an NTSTATUS error code from the miniport.
 */
NTSTATUS
SdPortSetBusWidth(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ UCHAR BusWidth)
{
    SDPORT_BUS_OPERATION BusOp;
    NTSTATUS Status;

    DPRINT1("SdPortSetBusWidth: Width=%u\n", BusWidth);

    /* Validate the bus width parameter */
    if (BusWidth != 1 && BusWidth != 4 && BusWidth != 8)
    {
        DPRINT1("SdPortSetBusWidth: Invalid bus width %u\n", BusWidth);
        return STATUS_INVALID_PARAMETER;
    }

    /* Verify 8-bit support if requested */
    if (BusWidth == 8 && !SlotExtension->Capabilities.EightBitSupported)
    {
        DPRINT1("SdPortSetBusWidth: 8-bit not supported by this slot\n");
        return STATUS_NOT_SUPPORTED;
    }

    if (FdoExtension->MiniportInitData.IssueBusOperation == NULL)
    {
        SlotExtension->CurrentBusWidth = BusWidth;
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&BusOp, sizeof(BusOp));
    BusOp.Type = SdSetBusWidth;
    BusOp.Parameters.BusWidth = BusWidth;

    Status = FdoExtension->MiniportInitData.IssueBusOperation(
                FdoExtension->MiniportPrivateExtension,
                &BusOp);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortSetBusWidth: IssueBusOperation failed (0x%08lx)\n", Status);
        return Status;
    }

    SlotExtension->CurrentBusWidth = BusWidth;
    return STATUS_SUCCESS;
}

/**
 * @brief Switch the signaling voltage for UHS-I modes via the miniport.
 *
 * Issues a SdSetSignalingVoltage bus operation. This involves the host
 * controller's Host Control 2 register (V18 signal enable bit) and may
 * require the miniport to perform a specific voltage switching sequence.
 *
 * @param[in] FdoExtension   Pointer to the FDO device extension.
 * @param[in] SlotExtension  Pointer to the slot extension; used to check
 *                           1.8V capability.
 * @param[in] Voltage        Signaling voltage selector (0 = 3.3V, 1 = 1.8V).
 *
 * @return STATUS_SUCCESS on success, STATUS_NOT_SUPPORTED if 1.8V is
 *         requested but the slot does not support it, or an NTSTATUS error
 *         code from the miniport bus operation.
 */
NTSTATUS
SdPortSetSignalingVoltage(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ UCHAR Voltage)
{
    SDPORT_BUS_OPERATION BusOp;
    NTSTATUS Status;

    DPRINT1("SdPortSetSignalingVoltage: Voltage=%u (%s)\n",
           Voltage, Voltage ? "1.8V" : "3.3V");

    /* Verify 1.8V support if requested */
    if (Voltage == 1 && !SlotExtension->Capabilities.V18Supported)
    {
        DPRINT1("SdPortSetSignalingVoltage: 1.8V not supported\n");
        return STATUS_NOT_SUPPORTED;
    }

    if (FdoExtension->MiniportInitData.IssueBusOperation == NULL)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&BusOp, sizeof(BusOp));
    BusOp.Type = SdSetSignalingVoltage;
    BusOp.Parameters.SignalingVoltage = Voltage;

    Status = FdoExtension->MiniportInitData.IssueBusOperation(
                FdoExtension->MiniportPrivateExtension,
                &BusOp);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortSetSignalingVoltage: IssueBusOperation failed (0x%08lx)\n",
                Status);
    }

    return Status;
}

NTSTATUS
SdPortExecuteTuning(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ UCHAR TuningCommand)
{
    SDPORT_BUS_OPERATION BusOp;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(SlotExtension);

    DPRINT1("SdPortExecuteTuning: TuningCommand=CMD%u\n", TuningCommand);

    if (TuningCommand != 19 && TuningCommand != 21)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FdoExtension->MiniportInitData.IssueBusOperation == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&BusOp, sizeof(BusOp));
    BusOp.Type = SdExecuteTuning;
    BusOp.Parameters.TuningCommand = TuningCommand;

    Status = FdoExtension->MiniportInitData.IssueBusOperation(
                FdoExtension->MiniportPrivateExtension,
                &BusOp);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortExecuteTuning: IssueBusOperation failed (0x%08lx)\n",
                Status);
    }

    return Status;
}
