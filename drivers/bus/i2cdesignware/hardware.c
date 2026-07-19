/*
 * PROJECT:     ReactOS Intel LPSS / DesignWare I2C driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Intel LPSS initialization and polled DesignWare I2C master
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "i2cdesignware.h"

#define NDEBUG
#include <debug.h>

#define LPSS_PRIV_OFFSET                    0x200
#define LPSS_PRIV_RESETS                    (LPSS_PRIV_OFFSET + 0x04)
#define LPSS_PRIV_RESETS_IDMA               0x00000004
#define LPSS_PRIV_RESETS_FUNC               0x00000003
#define LPSS_PRIV_REMAP_ADDR                (LPSS_PRIV_OFFSET + 0x40)
#define LPSS_PRIV_CAPS                      (LPSS_PRIV_OFFSET + 0xFC)
#define LPSS_PRIV_CAPS_TYPE_MASK            0x000000F0
#define LPSS_PRIV_CAPS_TYPE_I2C             0x00000000

#define DW_IC_CON                           0x00
#define DW_IC_TAR                           0x04
#define DW_IC_DATA_CMD                      0x10
#define DW_IC_SS_SCL_HCNT                   0x14
#define DW_IC_SS_SCL_LCNT                   0x18
#define DW_IC_FS_SCL_HCNT                   0x1C
#define DW_IC_FS_SCL_LCNT                   0x20
#define DW_IC_INTR_MASK                     0x30
#define DW_IC_RAW_INTR_STAT                 0x34
#define DW_IC_RX_TL                         0x38
#define DW_IC_TX_TL                         0x3C
#define DW_IC_CLR_INTR                      0x40
#define DW_IC_CLR_TX_ABRT                   0x54
#define DW_IC_CLR_STOP_DET                  0x60
#define DW_IC_ENABLE                        0x6C
#define DW_IC_STATUS                        0x70
#define DW_IC_TXFLR                         0x74
#define DW_IC_RXFLR                         0x78
#define DW_IC_SDA_HOLD                      0x7C
#define DW_IC_TX_ABRT_SOURCE                0x80
#define DW_IC_ENABLE_STATUS                 0x9C
#define DW_IC_SMBUS_INTR_MASK               0xCC
#define DW_IC_COMP_PARAM_1                  0xF4
#define DW_IC_COMP_VERSION                  0xF8
#define DW_IC_COMP_TYPE                     0xFC

#define DW_IC_COMP_TYPE_VALUE               0x44570140
#define DW_IC_SDA_HOLD_MIN_VERSION          0x3131312A

#define DW_IC_CON_MASTER                    0x00000001
#define DW_IC_CON_SPEED_STANDARD            0x00000002
#define DW_IC_CON_SPEED_FAST                0x00000004
#define DW_IC_CON_10BITADDR_MASTER          0x00000010
#define DW_IC_CON_RESTART_EN                0x00000020
#define DW_IC_CON_SLAVE_DISABLE             0x00000040

#define DW_IC_TAR_10BITADDR_MASTER          0x00001000

#define DW_IC_DATA_CMD_READ                 0x00000100
#define DW_IC_DATA_CMD_STOP                 0x00000200
#define DW_IC_DATA_CMD_RESTART              0x00000400

#define DW_IC_INTR_RX_UNDER                 0x00000001
#define DW_IC_INTR_RX_OVER                  0x00000002
#define DW_IC_INTR_TX_OVER                  0x00000008
#define DW_IC_INTR_TX_ABRT                  0x00000040
#define DW_IC_INTR_STOP_DET                 0x00000200

#define DW_IC_ENABLE_ENABLE                 0x00000001
#define DW_IC_ENABLE_ABORT                  0x00000002

#define DW_IC_STATUS_MASTER_ACTIVITY        0x00000020

#define DW_IC_TX_ABRT_NOACK_MASK            0x0000001F
#define DW_IC_TX_ABRT_ARB_LOST              0x00001000

#define DW_I2C_DEFAULT_CLOCK_HZ             133000000
#define DW_I2C_SDA_FALL_NS                  171
#define DW_I2C_SCL_FALL_NS                  208
#define DW_I2C_SDA_HOLD_NS                  42
#define DW_I2C_MAX_TRANSFER_BYTES           65535

static __inline
ULONG
DwReadRegister(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(DeviceExtension->Registers + Offset));
}

static __inline
VOID
DwWriteRegister(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(DeviceExtension->Registers + Offset), Value);
}

static
ULONG
DwNanosecondsToCycles(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Nanoseconds)
{
    ULONG ClockKHz = DeviceExtension->ControllerClockHz / 1000;

    return (ClockKHz * Nanoseconds + 500000) / 1000000;
}

static
ULONG
DwSclHighCount(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG HighNanoseconds)
{
    ULONG Count = DwNanosecondsToCycles(DeviceExtension,
                                        HighNanoseconds + DW_I2C_SDA_FALL_NS);

    return Count > 8 ? Count - 3 : 6;
}

static
ULONG
DwSclLowCount(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG LowNanoseconds)
{
    ULONG Count = DwNanosecondsToCycles(DeviceExtension,
                                        LowNanoseconds + DW_I2C_SCL_FALL_NS);

    return Count > 9 ? Count - 1 : 8;
}

static
NTSTATUS
DwWaitForMasterIdle(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG TimeoutMicroseconds)
{
    ULONG Index;

    for (Index = 0; Index < TimeoutMicroseconds / 5; Index++)
    {
        if (!(DwReadRegister(DeviceExtension, DW_IC_STATUS) &
              DW_IC_STATUS_MASTER_ACTIVITY))
        {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(5);
    }

    return STATUS_DEVICE_BUSY;
}

static
NTSTATUS
DwDisableController(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;

    DwWriteRegister(DeviceExtension, DW_IC_ENABLE, 0);
    for (Index = 0; Index < 2000; Index++)
    {
        if (!(DwReadRegister(DeviceExtension, DW_IC_ENABLE_STATUS) &
              DW_IC_ENABLE_ENABLE))
        {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(5);
    }

    return STATUS_IO_TIMEOUT;
}

static
VOID
DwAbortController(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;

    DwWriteRegister(DeviceExtension,
                    DW_IC_ENABLE,
                    DW_IC_ENABLE_ENABLE | DW_IC_ENABLE_ABORT);
    for (Index = 0; Index < 2000; Index++)
    {
        if (!(DwReadRegister(DeviceExtension, DW_IC_ENABLE) &
              DW_IC_ENABLE_ABORT))
        {
            break;
        }
        KeStallExecutionProcessor(5);
    }
    DwDisableController(DeviceExtension);
}

static
NTSTATUS
DwProgramController(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PREACTOS_SPB_TRANSFER_LIST TransferList)
{
    ULONG Control, Target, Speed;
    BOOLEAN TenBit;
    NTSTATUS Status;

    Speed = TransferList->ConnectionSpeed;
    if (Speed == 0)
        Speed = 400000;
    if (Speed > 1000000)
        return STATUS_NOT_SUPPORTED;

    TenBit = !!(TransferList->TypeSpecificFlags & 0x1);
    if ((!TenBit && TransferList->SlaveAddress > 0x7F) ||
        (TenBit && TransferList->SlaveAddress > 0x3FF))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DwWaitForMasterIdle(DeviceExtension, 20000);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DwDisableController(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;

    Control = DW_IC_CON_MASTER | DW_IC_CON_RESTART_EN |
              DW_IC_CON_SLAVE_DISABLE;
    if (Speed <= 100000)
    {
        Control |= DW_IC_CON_SPEED_STANDARD;
    }
    else
    {
        Control |= DW_IC_CON_SPEED_FAST;
        if (Speed > 400000)
        {
            DwWriteRegister(DeviceExtension,
                            DW_IC_FS_SCL_HCNT,
                            DwSclHighCount(DeviceExtension, 260));
            DwWriteRegister(DeviceExtension,
                            DW_IC_FS_SCL_LCNT,
                            DwSclLowCount(DeviceExtension, 500));
        }
        else
        {
            DwWriteRegister(DeviceExtension,
                            DW_IC_FS_SCL_HCNT,
                            DwSclHighCount(DeviceExtension, 600));
            DwWriteRegister(DeviceExtension,
                            DW_IC_FS_SCL_LCNT,
                            DwSclLowCount(DeviceExtension, 1300));
        }
    }

    Target = TransferList->SlaveAddress;
    if (TenBit)
    {
        Control |= DW_IC_CON_10BITADDR_MASTER;
        Target |= DW_IC_TAR_10BITADDR_MASTER;
    }

    DwWriteRegister(DeviceExtension, DW_IC_CON, Control);
    DwWriteRegister(DeviceExtension, DW_IC_TAR, Target);
    DwWriteRegister(DeviceExtension, DW_IC_INTR_MASK, 0);
    DwWriteRegister(DeviceExtension, DW_IC_SMBUS_INTR_MASK, 0);
    DwWriteRegister(DeviceExtension, DW_IC_RX_TL, 0);
    DwWriteRegister(DeviceExtension, DW_IC_TX_TL, 0);
    (VOID)DwReadRegister(DeviceExtension, DW_IC_CLR_INTR);
    DwWriteRegister(DeviceExtension, DW_IC_ENABLE, DW_IC_ENABLE_ENABLE);
    (VOID)DwReadRegister(DeviceExtension, DW_IC_ENABLE_STATUS);
    return STATUS_SUCCESS;
}

static
NTSTATUS
DwMapAbortStatus(
    _In_ ULONG AbortSource)
{
    if (AbortSource & DW_IC_TX_ABRT_NOACK_MASK)
        return STATUS_NO_SUCH_DEVICE;
    if (AbortSource & DW_IC_TX_ABRT_ARB_LOST)
        return STATUS_DEVICE_BUSY;
    return STATUS_IO_DEVICE_ERROR;
}

NTSTATUS
DwInitializeHardware(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Capabilities, ComponentType, ComponentParameters, HoldCount;
    NTSTATUS Status;

    if (DeviceExtension->Registers == NULL ||
        DeviceExtension->RegistersLength < LPSS_PRIV_OFFSET + 0x100)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    DeviceExtension->ControllerClockHz = DW_I2C_DEFAULT_CLOCK_HZ;

    Capabilities = DwReadRegister(DeviceExtension, LPSS_PRIV_CAPS);
    if ((Capabilities & LPSS_PRIV_CAPS_TYPE_MASK) != LPSS_PRIV_CAPS_TYPE_I2C)
    {
        DPRINT1("i2cdesignware: LPSS capability 0x%08lx is not I2C\n",
                Capabilities);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    DwWriteRegister(DeviceExtension, LPSS_PRIV_RESETS, 0);
    KeStallExecutionProcessor(10);
    DwWriteRegister(DeviceExtension,
                    LPSS_PRIV_RESETS,
                    LPSS_PRIV_RESETS_FUNC | LPSS_PRIV_RESETS_IDMA);
    DwWriteRegister(DeviceExtension,
                    LPSS_PRIV_REMAP_ADDR,
                    DeviceExtension->PhysicalBase.LowPart);
    DwWriteRegister(DeviceExtension,
                    LPSS_PRIV_REMAP_ADDR + sizeof(ULONG),
                    DeviceExtension->PhysicalBase.HighPart);

    ComponentType = DwReadRegister(DeviceExtension, DW_IC_COMP_TYPE);
    if (ComponentType != DW_IC_COMP_TYPE_VALUE)
    {
        DPRINT1("i2cdesignware: unexpected component type 0x%08lx\n",
                ComponentType);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ComponentParameters = DwReadRegister(DeviceExtension, DW_IC_COMP_PARAM_1);
    DeviceExtension->TxFifoDepth = ((ComponentParameters >> 16) & 0xFF) + 1;
    DeviceExtension->RxFifoDepth = ((ComponentParameters >> 8) & 0xFF) + 1;
    if (DeviceExtension->TxFifoDepth < 2 ||
        DeviceExtension->RxFifoDepth < 2)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = DwDisableController(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;

    DwWriteRegister(DeviceExtension,
                    DW_IC_SS_SCL_HCNT,
                    DwSclHighCount(DeviceExtension, 4000));
    DwWriteRegister(DeviceExtension,
                    DW_IC_SS_SCL_LCNT,
                    DwSclLowCount(DeviceExtension, 4700));
    DwWriteRegister(DeviceExtension,
                    DW_IC_FS_SCL_HCNT,
                    DwSclHighCount(DeviceExtension, 600));
    DwWriteRegister(DeviceExtension,
                    DW_IC_FS_SCL_LCNT,
                    DwSclLowCount(DeviceExtension, 1300));

    if (DwReadRegister(DeviceExtension, DW_IC_COMP_VERSION) >=
        DW_IC_SDA_HOLD_MIN_VERSION)
    {
        HoldCount = DwNanosecondsToCycles(DeviceExtension,
                                          DW_I2C_SDA_HOLD_NS);
        if (HoldCount == 0)
            HoldCount = 1;
        DwWriteRegister(DeviceExtension,
                        DW_IC_SDA_HOLD,
                        HoldCount | (1 << 16));
    }

    DwWriteRegister(DeviceExtension, DW_IC_INTR_MASK, 0);
    DwWriteRegister(DeviceExtension, DW_IC_SMBUS_INTR_MASK, 0);
    (VOID)DwReadRegister(DeviceExtension, DW_IC_CLR_INTR);
    DPRINT1("i2cdesignware: DesignWare I2C ready, TX FIFO %lu, RX FIFO %lu\n",
            DeviceExtension->TxFifoDepth,
            DeviceExtension->RxFifoDepth);
    return STATUS_SUCCESS;
}

VOID
DwQuiesceHardware(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->Registers == NULL)
        return;

    DwDisableController(DeviceExtension);
    DwWriteRegister(DeviceExtension, LPSS_PRIV_RESETS, 0);
}

NTSTATUS
DwExecuteTransferList(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PREACTOS_SPB_TRANSFER_LIST TransferList,
    _Out_ PULONG_PTR Information)
{
    ULONG CommandTransfer = 0, CommandOffset = 0;
    ULONG ReceiveTransfer = 0, ReceiveOffset = 0;
    ULONG OutstandingReads = 0, TotalLength = 0;
    ULONG TxFree, RxLevel, RawInterrupts, AbortSource;
    ULONG Command, Index, Value, TimeoutMilliseconds;
    ULONGLONG Deadline;
    BOOLEAN CommandsComplete, Progress;
    PREACTOS_SPB_TRANSFER Transfer;
    NTSTATUS Status;

    *Information = 0;
    if (TransferList->Version != REACTOS_SPB_INTERFACE_VERSION ||
        TransferList->TransferCount == 0 ||
        TransferList->TransferCount > REACTOS_SPB_MAX_TRANSFERS ||
        (TransferList->Flags & ~REACTOS_SPB_TRANSFER_FLAG_CONTROLLER_LOCKED))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (Index = 0; Index < TransferList->TransferCount; Index++)
    {
        Transfer = &TransferList->Transfers[Index];
        if ((Transfer->Direction != SpbTransferDirectionFromDevice &&
             Transfer->Direction != SpbTransferDirectionToDevice) ||
            Transfer->Buffer == NULL || Transfer->Length == 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (Transfer->DelayInUs != 0)
            return STATUS_NOT_SUPPORTED;
        if (Transfer->Length > DW_I2C_MAX_TRANSFER_BYTES - TotalLength)
            return STATUS_INVALID_BUFFER_SIZE;
        TotalLength += Transfer->Length;
    }

    Status = DwProgramController(DeviceExtension, TransferList);
    if (!NT_SUCCESS(Status))
        return Status;

    TimeoutMilliseconds = 100 + TotalLength / 2;
    if (TimeoutMilliseconds > 5000)
        TimeoutMilliseconds = 5000;
    Deadline = KeQueryInterruptTime() +
               (ULONGLONG)TimeoutMilliseconds * 10000;

    for (;;)
    {
        Progress = FALSE;
        RawInterrupts = DwReadRegister(DeviceExtension, DW_IC_RAW_INTR_STAT);
        if (RawInterrupts & DW_IC_INTR_TX_ABRT)
        {
            AbortSource = DwReadRegister(DeviceExtension,
                                         DW_IC_TX_ABRT_SOURCE);
            (VOID)DwReadRegister(DeviceExtension, DW_IC_CLR_TX_ABRT);
            DPRINT1("i2cdesignware: transfer aborted, source 0x%08lx\n",
                    AbortSource);
            Status = DwMapAbortStatus(AbortSource);
            goto Done;
        }
        if (RawInterrupts & (DW_IC_INTR_RX_UNDER |
                             DW_IC_INTR_RX_OVER |
                             DW_IC_INTR_TX_OVER))
        {
            Status = STATUS_IO_DEVICE_ERROR;
            goto Done;
        }

        RxLevel = DwReadRegister(DeviceExtension, DW_IC_RXFLR);
        while (RxLevel-- != 0)
        {
            while (ReceiveTransfer < TransferList->TransferCount &&
                   TransferList->Transfers[ReceiveTransfer].Direction !=
                       SpbTransferDirectionFromDevice)
            {
                ReceiveTransfer++;
                ReceiveOffset = 0;
            }

            if (ReceiveTransfer >= TransferList->TransferCount ||
                OutstandingReads == 0)
            {
                Status = STATUS_IO_DEVICE_ERROR;
                goto Done;
            }

            Transfer = &TransferList->Transfers[ReceiveTransfer];
            Value = DwReadRegister(DeviceExtension, DW_IC_DATA_CMD);
            ((PUCHAR)Transfer->Buffer)[ReceiveOffset++] = (UCHAR)Value;
            OutstandingReads--;
            Progress = TRUE;
            if (ReceiveOffset == Transfer->Length)
            {
                ReceiveTransfer++;
                ReceiveOffset = 0;
            }
        }

        while (CommandTransfer < TransferList->TransferCount &&
               CommandOffset ==
                   TransferList->Transfers[CommandTransfer].Length)
        {
            CommandTransfer++;
            CommandOffset = 0;
        }

        TxFree = DeviceExtension->TxFifoDepth -
                 DwReadRegister(DeviceExtension, DW_IC_TXFLR);
        while (CommandTransfer < TransferList->TransferCount && TxFree != 0)
        {
            Transfer = &TransferList->Transfers[CommandTransfer];
            if (Transfer->Direction == SpbTransferDirectionFromDevice &&
                OutstandingReads >= DeviceExtension->RxFifoDepth)
            {
                break;
            }

            Command = 0;
            if (Transfer->Direction == SpbTransferDirectionFromDevice)
                Command |= DW_IC_DATA_CMD_READ;
            else
                Command |= ((PUCHAR)Transfer->Buffer)[CommandOffset];

            if (CommandTransfer != 0 && CommandOffset == 0)
                Command |= DW_IC_DATA_CMD_RESTART;
            if (CommandTransfer == TransferList->TransferCount - 1 &&
                CommandOffset == Transfer->Length - 1)
            {
                Command |= DW_IC_DATA_CMD_STOP;
            }

            DwWriteRegister(DeviceExtension, DW_IC_DATA_CMD, Command);
            if (Transfer->Direction == SpbTransferDirectionFromDevice)
                OutstandingReads++;
            CommandOffset++;
            TxFree--;
            Progress = TRUE;

            if (CommandOffset == Transfer->Length)
            {
                CommandTransfer++;
                CommandOffset = 0;
            }
        }

        CommandsComplete = (CommandTransfer >= TransferList->TransferCount);
        RawInterrupts = DwReadRegister(DeviceExtension, DW_IC_RAW_INTR_STAT);
        if (CommandsComplete && OutstandingReads == 0 &&
            (RawInterrupts & DW_IC_INTR_STOP_DET))
        {
            (VOID)DwReadRegister(DeviceExtension, DW_IC_CLR_STOP_DET);
            Status = DwWaitForMasterIdle(DeviceExtension, 20000);
            if (NT_SUCCESS(Status))
                *Information = TotalLength;
            goto Done;
        }

        if (KeQueryInterruptTime() >= Deadline)
        {
            DPRINT1("i2cdesignware: transfer timed out\n");
            DwAbortController(DeviceExtension);
            return STATUS_IO_TIMEOUT;
        }

        if (!Progress)
            KeStallExecutionProcessor(5);
    }

Done:
    DwDisableController(DeviceExtension);
    (VOID)DwReadRegister(DeviceExtension, DW_IC_CLR_INTR);
    return Status;
}
