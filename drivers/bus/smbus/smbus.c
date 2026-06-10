/*
 * PROJECT:     ReactOS SMBus Controller Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Intel ICH/PCH (i801-compatible) SMBus host controller driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This is a clean-room implementation based on the public Intel ICH/PCH
 * datasheets and the SMBus 2.0/3.0 specifications.
 */

#include "smbus.h"

#include <initguid.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

#define SMBUS_TEMP_BOOT_PROBE 1

/*
 * Device interface other drivers (battery, thermal, SPD/EEPROM, sensors) and
 * user-mode components use to locate and open the SMBus controller before
 * sending IOCTL_SMBUS_EXECUTE.  This avoids every consumer hard-coding PCI
 * access to the controller.
 */
DEFINE_GUID(GUID_DEVINTERFACE_SMBUS,
            0x3a8c9f12, 0x7b6d, 0x4e5a, 0xb2, 0xc1, 0x9d, 0x8e, 0x7f, 0x6a, 0x5b, 0x4c);

/* -------------------------------------------------------------------------- */
/* Low-level register access                                                  */
/* -------------------------------------------------------------------------- */

static UCHAR
SmbusReadByte(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset)
{
    return READ_PORT_UCHAR(DeviceExtension->IoBase + Offset);
}

static VOID
SmbusWriteByte(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset,
    _In_ UCHAR Value)
{
    WRITE_PORT_UCHAR(DeviceExtension->IoBase + Offset, Value);
}

static VOID
SmbusClearStatus(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    SmbusWriteByte(DeviceExtension, SMBHSTSTS, SMBHSTSTS_CLEAR);
}

/*
 * Reading SMBHSTSTS acquires the INUSE semaphore on i801-compatible
 * controllers.  Release it only after we are done with the register sequence,
 * not during the pre-transaction status clear.
 */
static VOID
SmbusReleaseHostSemaphore(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    SmbusWriteByte(DeviceExtension, SMBHSTSTS, SMBHSTSTS_INUSE);
}

static VOID
SmbusFinishStatus(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    SmbusWriteByte(DeviceExtension,
                   SMBHSTSTS,
                   SMBHSTSTS_INUSE | SMBHSTSTS_CLEAR);
}

/* -------------------------------------------------------------------------- */
/* PCI configuration-space access through the parent bus interface            */
/* -------------------------------------------------------------------------- */

static BOOLEAN
SmbusReadConfigByte(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset,
    _Out_ PUCHAR Value)
{
    if (!DeviceExtension->BusInterfaceAcquired)
        return FALSE;

    return DeviceExtension->BusInterface.GetBusData(
               DeviceExtension->BusInterface.Context,
               PCI_WHICHSPACE_CONFIG,
               Value,
               Offset,
               sizeof(*Value)) == sizeof(*Value);
}

static BOOLEAN
SmbusWriteConfigByte(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Offset,
    _In_ UCHAR Value)
{
    if (!DeviceExtension->BusInterfaceAcquired)
        return FALSE;

    return DeviceExtension->BusInterface.SetBusData(
               DeviceExtension->BusInterface.Context,
               PCI_WHICHSPACE_CONFIG,
               &Value,
               Offset,
               sizeof(Value)) == sizeof(Value);
}

static NTSTATUS
SmbusAcquireBusInterface(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;
    PIO_STACK_LOCATION IrpStack;
    NTSTATUS Status;

    if (DeviceExtension->BusInterfaceAcquired)
        return STATUS_SUCCESS;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       DeviceExtension->LowerDevice,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    IrpStack = IoGetNextIrpStackLocation(Irp);
    IrpStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IrpStack->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
    IrpStack->Parameters.QueryInterface.Version = PCI_BUS_INTERFACE_STANDARD_VERSION;
    IrpStack->Parameters.QueryInterface.Size = sizeof(BUS_INTERFACE_STANDARD);
    IrpStack->Parameters.QueryInterface.Interface =
        (PINTERFACE)&DeviceExtension->BusInterface;
    IrpStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (NT_SUCCESS(Status))
        DeviceExtension->BusInterfaceAcquired = TRUE;

    return Status;
}

static VOID
SmbusReleaseBusInterface(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    if (!DeviceExtension->BusInterfaceAcquired)
        return;

    if (DeviceExtension->BusInterface.InterfaceDereference != NULL)
    {
        DeviceExtension->BusInterface.InterfaceDereference(
            DeviceExtension->BusInterface.Context);
    }

    DeviceExtension->BusInterfaceAcquired = FALSE;
}

/*
 * Make sure the host controller is enabled and uses SMBus (not I2C) bus
 * timing.  Some firmware leaves the controller disabled; without HST_EN set
 * the I/O registers do not respond.  The original value is saved so it can be
 * restored when the device is stopped or removed.
 */
static BOOLEAN
SmbusEnableHostController(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR Config;
    UCHAR NewConfig;
    UCHAR ActiveConfig;

    if (!SmbusReadConfigByte(DeviceExtension, SMBUS_PCI_HSTCFG, &Config))
        return FALSE;

    if (!DeviceExtension->ConfigSaved)
    {
        DeviceExtension->OriginalConfig = Config;
        DeviceExtension->ConfigSaved = TRUE;
    }

    if (!(Config & SMBHSTCFG_HST_EN))
        DPRINT1("SMBus: enabling host controller (HSTCFG 0x%02x)\n", Config);

    NewConfig = Config;
    NewConfig &= ~SMBHSTCFG_I2C_EN;
    NewConfig |= SMBHSTCFG_HST_EN;

    if (!SmbusWriteConfigByte(DeviceExtension, SMBUS_PCI_HSTCFG, NewConfig))
        return FALSE;

    if (!SmbusReadConfigByte(DeviceExtension, SMBUS_PCI_HSTCFG, &ActiveConfig))
        return FALSE;

    DPRINT1("SMBus: HSTCFG before 0x%02x requested 0x%02x active 0x%02x\n",
            Config, NewConfig, ActiveConfig);

    return (ActiveConfig & SMBHSTCFG_HST_EN) != 0;
}

static VOID
SmbusRestoreHostController(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->ConfigSaved)
    {
        if (SmbusWriteConfigByte(DeviceExtension,
                                 SMBUS_PCI_HSTCFG,
                                 DeviceExtension->OriginalConfig))
        {
            DeviceExtension->ConfigSaved = FALSE;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Capability detection                                                       */
/* -------------------------------------------------------------------------- */

/*
 * The block buffer (E32B) and hardware PEC (CRC) features live in the
 * auxiliary control register.  Probe them by attempting to set the bits and
 * checking whether they stick, then leave the register cleared as the
 * datasheet recommends at initialization time.
 */
static VOID
SmbusDetectCapabilities(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR Aux;
    UCHAR Probe;

    DeviceExtension->Capabilities = 0;

    if (DeviceExtension->IoLength <= SMBAUXCTL)
        return;

    Aux = SmbusReadByte(DeviceExtension, SMBAUXCTL);
    SmbusWriteByte(DeviceExtension, SMBAUXCTL, Aux | SMBAUXCTL_E32B | SMBAUXCTL_CRC);
    Probe = SmbusReadByte(DeviceExtension, SMBAUXCTL);
    SmbusWriteByte(DeviceExtension, SMBAUXCTL, Aux & ~(SMBAUXCTL_E32B | SMBAUXCTL_CRC));

    if (Probe & SMBAUXCTL_E32B)
        DeviceExtension->Capabilities |= SMBUS_CAP_BLOCK_BUFFER;
    if (Probe & SMBAUXCTL_CRC)
        DeviceExtension->Capabilities |= SMBUS_CAP_PEC;
}

static BOOLEAN
SmbusCheckClearPecError(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR Aux;

    if (!(DeviceExtension->Capabilities & SMBUS_CAP_PEC))
        return FALSE;

    Aux = SmbusReadByte(DeviceExtension, SMBAUXSTS) & SMBAUXSTS_CRCE;
    if (Aux)
    {
        SmbusWriteByte(DeviceExtension, SMBAUXSTS, Aux);
        return TRUE;
    }

    return FALSE;
}

/* -------------------------------------------------------------------------- */
/* Bus state machine                                                          */
/* -------------------------------------------------------------------------- */

static NTSTATUS
SmbusWaitWhileBusy(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Count;

    for (Count = 0; Count < SMBUS_MAX_POLL_COUNT; Count++)
    {
        if (!(SmbusReadByte(DeviceExtension, SMBHSTSTS) & SMBHSTSTS_BUSY))
            return STATUS_SUCCESS;

        KeStallExecutionProcessor(50);
    }

    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
SmbusWaitForTransaction(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _Out_ PUCHAR FinalStatus)
{
    ULONG Count;
    UCHAR Status;

    for (Count = 0; Count < SMBUS_MAX_POLL_COUNT; Count++)
    {
        Status = SmbusReadByte(DeviceExtension, SMBHSTSTS);

        /*
         * Only act once the controller has actually gone idle (HOST_BUSY
         * clear) AND signalled completion or an error.  Reading result/block
         * data while the controller is still busy can return stale or partial
         * data on some PCH steppings.
         */
        if (!(Status & SMBHSTSTS_BUSY) &&
            (Status & (SMBHSTSTS_INTR | SMBHSTSTS_ERROR)))
        {
            *FinalStatus = Status;

            if (Status & SMBHSTSTS_FAILED)
                return STATUS_UNSUCCESSFUL;
            if (Status & SMBHSTSTS_BUS_ERR)
                return STATUS_DATA_OVERRUN;
            if (Status & SMBHSTSTS_DEV_ERR)
                return STATUS_DEVICE_PROTOCOL_ERROR;

            return STATUS_SUCCESS;
        }

        KeStallExecutionProcessor(50);
    }

    *FinalStatus = SmbusReadByte(DeviceExtension, SMBHSTSTS);
    return STATUS_IO_TIMEOUT;
}

/*
 * Bring the controller back to an idle state.  First try to clear any latched
 * status; if it remains busy, issue a KILL to abort whatever transaction the
 * controller (or firmware) left running, then clear status again.
 */
static NTSTATUS
SmbusRecoverController(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    SmbusClearStatus(DeviceExtension);
    Status = SmbusWaitWhileBusy(DeviceExtension);
    if (NT_SUCCESS(Status))
    {
        SmbusReleaseHostSemaphore(DeviceExtension);
        return STATUS_SUCCESS;
    }

    SmbusWriteByte(DeviceExtension, SMBHSTCNT, SMBHSTCNT_KILL);
    KeStallExecutionProcessor(100);
    SmbusWriteByte(DeviceExtension, SMBHSTCNT, 0);
    SmbusClearStatus(DeviceExtension);

    Status = SmbusWaitWhileBusy(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SMBus: controller remains busy, status 0x%02x\n",
                SmbusReadByte(DeviceExtension, SMBHSTSTS));
        SmbusFinishStatus(DeviceExtension);
        return STATUS_DEVICE_BUSY;
    }

    SmbusFinishStatus(DeviceExtension);
    return STATUS_SUCCESS;
}

/* Program the slave address byte (7-bit address plus direction). */
static VOID
SmbusSetAddress(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Address,
    _In_ BOOLEAN Read)
{
    SmbusWriteByte(DeviceExtension, SMBHSTADD,
                   (UCHAR)((Address << 1) | (Read ? 1 : 0)));
}

/* Kick off a programmed transaction and wait for it to retire. */
static NTSTATUS
SmbusRunCommand(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Control,
    _Out_ PUCHAR FinalStatus)
{
    SmbusWriteByte(DeviceExtension, SMBHSTCNT, (UCHAR)(Control | SMBHSTCNT_START));
    return SmbusWaitForTransaction(DeviceExtension, FinalStatus);
}

/* -------------------------------------------------------------------------- */
/* Protocol handlers                                                          */
/* -------------------------------------------------------------------------- */

static NTSTATUS
SmbusSimpleTransaction(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PSMBUS_TRANSACTION Transaction,
    _Out_ PUCHAR FinalStatus)
{
    NTSTATUS Status;
    UCHAR Control;
    BOOLEAN Read = Transaction->Read;

    switch ((SMBUS_TRANSFER_PROTOCOL)Transaction->Protocol)
    {
        case SmbusTransferQuick:
            SmbusSetAddress(DeviceExtension, Transaction->Address, Read);
            Control = SMBHSTCNT_QUICK;
            break;

        case SmbusTransferByte:
            SmbusSetAddress(DeviceExtension, Transaction->Address, Read);
            if (!Read)
                SmbusWriteByte(DeviceExtension, SMBHSTCMD, Transaction->Command);
            Control = SMBHSTCNT_BYTE;
            break;

        case SmbusTransferByteData:
            SmbusSetAddress(DeviceExtension, Transaction->Address, Read);
            SmbusWriteByte(DeviceExtension, SMBHSTCMD, Transaction->Command);
            if (!Read)
                SmbusWriteByte(DeviceExtension, SMBHSTDAT0, (UCHAR)Transaction->Data);
            Control = SMBHSTCNT_BYTE_DATA;
            break;

        case SmbusTransferWordData:
            SmbusSetAddress(DeviceExtension, Transaction->Address, Read);
            SmbusWriteByte(DeviceExtension, SMBHSTCMD, Transaction->Command);
            if (!Read)
            {
                SmbusWriteByte(DeviceExtension, SMBHSTDAT0, (UCHAR)Transaction->Data);
                SmbusWriteByte(DeviceExtension, SMBHSTDAT1, (UCHAR)(Transaction->Data >> 8));
            }
            Control = SMBHSTCNT_WORD_DATA;
            break;

        case SmbusTransferProcessCall:
            /* Process call always writes the command word then reads a word. */
            SmbusSetAddress(DeviceExtension, Transaction->Address, FALSE);
            SmbusWriteByte(DeviceExtension, SMBHSTCMD, Transaction->Command);
            SmbusWriteByte(DeviceExtension, SMBHSTDAT0, (UCHAR)Transaction->Data);
            SmbusWriteByte(DeviceExtension, SMBHSTDAT1, (UCHAR)(Transaction->Data >> 8));
            Read = TRUE;
            Control = SMBHSTCNT_PROC_CALL;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    Status = SmbusRunCommand(DeviceExtension, Control, FinalStatus);
    if (!NT_SUCCESS(Status) || !Read)
        return Status;

    switch ((SMBUS_TRANSFER_PROTOCOL)Transaction->Protocol)
    {
        case SmbusTransferByte:
        case SmbusTransferByteData:
            Transaction->Data = SmbusReadByte(DeviceExtension, SMBHSTDAT0);
            break;

        case SmbusTransferWordData:
        case SmbusTransferProcessCall:
            Transaction->Data = SmbusReadByte(DeviceExtension, SMBHSTDAT0);
            Transaction->Data |= (USHORT)SmbusReadByte(DeviceExtension, SMBHSTDAT1) << 8;
            break;

        default:
            break;
    }

    return Status;
}

/*
 * Block read/write using the 32-byte block buffer (E32B).  Reading SMBHSTCNT
 * resets the auto-incrementing buffer index, after which the payload is
 * streamed through SMBBLKDAT in a single transaction.
 */
static NTSTATUS
SmbusBlockTransaction(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PSMBUS_TRANSACTION Transaction,
    _In_ UCHAR Control,
    _In_ BOOLEAN DoWrite,
    _In_ BOOLEAN DoRead,
    _Out_ PUCHAR FinalStatus)
{
    NTSTATUS Status;
    UCHAR Aux;
    UCHAR Length;
    ULONG Index;

    if (!(DeviceExtension->Capabilities & SMBUS_CAP_BLOCK_BUFFER))
        return STATUS_NOT_SUPPORTED;

    if (DoWrite &&
        (Transaction->BlockLength < 1 || Transaction->BlockLength > SMBUS_BLOCK_MAX))
    {
        return STATUS_INVALID_PARAMETER;
    }

    SmbusSetAddress(DeviceExtension, Transaction->Address, Transaction->Read);
    SmbusWriteByte(DeviceExtension, SMBHSTCMD, Transaction->Command);

    Aux = SmbusReadByte(DeviceExtension, SMBAUXCTL);
    SmbusWriteByte(DeviceExtension, SMBAUXCTL, Aux | SMBAUXCTL_E32B);

    if (DoWrite)
    {
        Length = Transaction->BlockLength;
        SmbusWriteByte(DeviceExtension, SMBHSTDAT0, Length);
        SmbusReadByte(DeviceExtension, SMBHSTCNT);      /* reset buffer index */
        for (Index = 0; Index < Length; Index++)
            SmbusWriteByte(DeviceExtension, SMBBLKDAT, Transaction->Block[Index]);
    }

    Status = SmbusRunCommand(DeviceExtension, Control, FinalStatus);

    if (NT_SUCCESS(Status) && DoRead)
    {
        Length = SmbusReadByte(DeviceExtension, SMBHSTDAT0);
        if (Length < 1 || Length > SMBUS_BLOCK_MAX)
        {
            DPRINT1("SMBus: illegal block read length %u\n", Length);
            Status = STATUS_DEVICE_PROTOCOL_ERROR;
        }
        else
        {
            Transaction->BlockLength = Length;
            SmbusReadByte(DeviceExtension, SMBHSTCNT);  /* reset buffer index */
            for (Index = 0; Index < Length; Index++)
                Transaction->Block[Index] = SmbusReadByte(DeviceExtension, SMBBLKDAT);
        }
    }

    Aux = SmbusReadByte(DeviceExtension, SMBAUXCTL);
    SmbusWriteByte(DeviceExtension, SMBAUXCTL, Aux & ~SMBAUXCTL_E32B);

    return Status;
}

static NTSTATUS
SmbusPerformTransaction(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PSMBUS_TRANSACTION Transaction)
{
    NTSTATUS Status;
    UCHAR Aux;
    UCHAR FinalStatus = 0;
    BOOLEAN UsePec;

    Status = SmbusWaitWhileBusy(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        SmbusRecoverController(DeviceExtension);
        SmbusFinishStatus(DeviceExtension);
        return Status;
    }

    SmbusClearStatus(DeviceExtension);
    SmbusCheckClearPecError(DeviceExtension);

    UsePec = (Transaction->Flags & SMBUS_TRANSACTION_FLAG_PEC) &&
             (DeviceExtension->Capabilities & SMBUS_CAP_PEC) &&
             (Transaction->Protocol != SmbusTransferQuick);

    Aux = SmbusReadByte(DeviceExtension, SMBAUXCTL);
    if (UsePec)
        SmbusWriteByte(DeviceExtension, SMBAUXCTL, Aux | SMBAUXCTL_CRC);
    else
        SmbusWriteByte(DeviceExtension, SMBAUXCTL, Aux & ~SMBAUXCTL_CRC);

    switch ((SMBUS_TRANSFER_PROTOCOL)Transaction->Protocol)
    {
        case SmbusTransferQuick:
        case SmbusTransferByte:
        case SmbusTransferByteData:
        case SmbusTransferWordData:
        case SmbusTransferProcessCall:
            Status = SmbusSimpleTransaction(DeviceExtension, Transaction, &FinalStatus);
            break;

        case SmbusTransferBlockData:
            Status = SmbusBlockTransaction(DeviceExtension,
                                           Transaction,
                                           SMBHSTCNT_BLOCK_DATA,
                                           !Transaction->Read,
                                           Transaction->Read,
                                           &FinalStatus);
            break;

        case SmbusTransferBlockProcessCall:
            /* Hardware can support this on newer parts; not implemented here. */
            Status = STATUS_NOT_SUPPORTED;
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    /*
     * A device error (DEV_ERR) means either a PEC mismatch or that the target
     * did not acknowledge.  AUXSTS is not auto-cleared by hardware, so check
     * and clear the CRC error flag to tell the two apart.
     */
    if (Status == STATUS_DEVICE_PROTOCOL_ERROR)
    {
        if (SmbusCheckClearPecError(DeviceExtension))
            Status = STATUS_CRC_ERROR;
        else
            Status = STATUS_NO_SUCH_DEVICE;     /* slave did not respond */
    }
    else if (UsePec)
    {
        SmbusCheckClearPecError(DeviceExtension);
    }

    if (UsePec)
    {
        /* Some firmware dislikes PEC being left enabled across resume/reboot. */
        Aux = SmbusReadByte(DeviceExtension, SMBAUXCTL);
        SmbusWriteByte(DeviceExtension, SMBAUXCTL, Aux & ~SMBAUXCTL_CRC);
    }

    if (Status == STATUS_IO_TIMEOUT)
        SmbusRecoverController(DeviceExtension);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SMBus: transaction (proto %u, addr 0x%02x) failed 0x%08lx, "
                "host status 0x%02x\n",
                Transaction->Protocol, Transaction->Address, Status, FinalStatus);
    }

    SmbusFinishStatus(DeviceExtension);

    return Status;
}

#if SMBUS_TEMP_BOOT_PROBE
static VOID
SmbusDebugDumpController(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_ PCSTR Stage)
{
    UCHAR HstCfg = 0;
    UCHAR HstSts;
    UCHAR HstCnt;
    UCHAR AuxSts = 0;
    UCHAR AuxCtl = 0;
    BOOLEAN ConfigValid;

    ConfigValid = SmbusReadConfigByte(DeviceExtension, SMBUS_PCI_HSTCFG, &HstCfg);
    HstSts = SmbusReadByte(DeviceExtension, SMBHSTSTS);
    HstCnt = SmbusReadByte(DeviceExtension, SMBHSTCNT);
    if (DeviceExtension->IoLength > SMBAUXCTL)
    {
        AuxSts = SmbusReadByte(DeviceExtension, SMBAUXSTS);
        AuxCtl = SmbusReadByte(DeviceExtension, SMBAUXCTL);
    }
    SmbusReleaseHostSemaphore(DeviceExtension);

    DPRINT1("SMBus: TEMP discovery %s HSTCFG %s0x%02x HSTSTS 0x%02x "
            "HSTCNT 0x%02x AUXSTS 0x%02x AUXCTL 0x%02x caps 0x%lx\n",
            Stage,
            ConfigValid ? "" : "read-failed/",
            HstCfg,
            HstSts,
            HstCnt,
            AuxSts,
            AuxCtl,
            DeviceExtension->Capabilities);
}

static NTSTATUS
SmbusDebugProbeAddress(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Address,
    _Out_ PUCHAR Data,
    _Out_ PUCHAR FinalStatus)
{
    NTSTATUS Status;

    *Data = 0;
    *FinalStatus = 0;

    Status = SmbusWaitWhileBusy(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        SmbusRecoverController(DeviceExtension);
        return Status;
    }

    SmbusClearStatus(DeviceExtension);
    SmbusCheckClearPecError(DeviceExtension);
    SmbusSetAddress(DeviceExtension, Address, TRUE);

    Status = SmbusRunCommand(DeviceExtension, SMBHSTCNT_BYTE, FinalStatus);
    if (NT_SUCCESS(Status))
    {
        *Data = SmbusReadByte(DeviceExtension, SMBHSTDAT0);
    }
    else if (Status == STATUS_DEVICE_PROTOCOL_ERROR &&
             (*FinalStatus & SMBHSTSTS_DEV_ERR))
    {
        Status = STATUS_NO_SUCH_DEVICE;
    }

    if (Status == STATUS_IO_TIMEOUT)
        SmbusRecoverController(DeviceExtension);

    SmbusFinishStatus(DeviceExtension);
    return Status;
}

static VOID
SmbusDebugProbeBus(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR Address;
    UCHAR Data;
    UCHAR FinalStatus;
    ULONG AckCount = 0;
    ULONG ErrorCount = 0;
    NTSTATUS Status;

    DPRINT1("SMBus: TEMP boot probe starting receive-byte scan 0x03-0x77; "
            "remove before push\n");

    ExAcquireFastMutex(&DeviceExtension->TransactionLock);
    for (Address = 0x03; Address <= 0x77; Address++)
    {
        Status = SmbusDebugProbeAddress(DeviceExtension,
                                        Address,
                                        &Data,
                                        &FinalStatus);
        if (NT_SUCCESS(Status))
        {
            AckCount++;
            DPRINT1("SMBus: TEMP probe addr 0x%02x ACK data 0x%02x "
                    "status 0x%02x\n",
                    Address,
                    Data,
                    FinalStatus);
        }
        else if (Status != STATUS_NO_SUCH_DEVICE)
        {
            ErrorCount++;
            DPRINT1("SMBus: TEMP probe addr 0x%02x failed 0x%08lx "
                    "status 0x%02x\n",
                    Address,
                    Status,
                    FinalStatus);
            if (ErrorCount >= 3)
            {
                DPRINT1("SMBus: TEMP probe stopped after %lu bus errors\n",
                        ErrorCount);
                break;
            }
        }
    }
    ExReleaseFastMutex(&DeviceExtension->TransactionLock);

    DPRINT1("SMBus: TEMP boot probe finished, ACK %lu, errors %lu\n",
            AckCount,
            ErrorCount);
}
#endif

static NTSTATUS
SmbusExecuteTransaction(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PSMBUS_TRANSACTION Transaction)
{
    NTSTATUS Status;

    if (!DeviceExtension->Started)
        return STATUS_DEVICE_NOT_READY;

    if (Transaction->Version != 1 ||
        Transaction->Protocol >= SmbusTransferMaximum ||
        Transaction->Address == 0 ||
        Transaction->Address >= 0x80)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Serialize: the controller has a single shared register set. */
    ExAcquireFastMutex(&DeviceExtension->TransactionLock);
    Status = SmbusPerformTransaction(DeviceExtension, Transaction);
    ExReleaseFastMutex(&DeviceExtension->TransactionLock);

    return Status;
}

/* -------------------------------------------------------------------------- */
/* PnP / hardware lifetime                                                    */
/* -------------------------------------------------------------------------- */

static NTSTATUS
SmbusForwardCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
SmbusForwardIrpSynchronously(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SmbusForwardCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    return Irp->IoStatus.Status;
}

static NTSTATUS
SmbusCompleteIrp(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static VOID
SmbusReleaseHardware(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension)
{
    DeviceExtension->Started = FALSE;
    DeviceExtension->IoBase = NULL;
    DeviceExtension->IoLength = 0;
    DeviceExtension->IoStart.QuadPart = 0;
    DeviceExtension->MmioStart.QuadPart = 0;
    DeviceExtension->MmioLength = 0;
    DeviceExtension->Capabilities = 0;
}

static NTSTATUS
SmbusStartHardware(
    _In_ PSMBUS_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PCM_RESOURCE_LIST RawResources,
    _In_opt_ PCM_RESOURCE_LIST TranslatedResources)
{
    ULONG ListIndex;
    ULONG ResourceIndex;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RawResources);

    SmbusReleaseHardware(DeviceExtension);

    if (TranslatedResources == NULL)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    for (ListIndex = 0; ListIndex < TranslatedResources->Count; ListIndex++)
    {
        FullDescriptor = &TranslatedResources->List[ListIndex];

        for (ResourceIndex = 0;
             ResourceIndex < FullDescriptor->PartialResourceList.Count;
             ResourceIndex++)
        {
            Descriptor = &FullDescriptor->PartialResourceList.PartialDescriptors[ResourceIndex];

            if (Descriptor->Type == CmResourceTypePort &&
                Descriptor->u.Port.Length >= SMBUS_MIN_IO_LENGTH &&
                DeviceExtension->IoBase == NULL)
            {
                DeviceExtension->IoStart = Descriptor->u.Port.Start;
                DeviceExtension->IoLength = Descriptor->u.Port.Length;
                DeviceExtension->IoBase =
                    (PUCHAR)(ULONG_PTR)Descriptor->u.Port.Start.QuadPart;
            }
            else if (Descriptor->Type == CmResourceTypeMemory &&
                     Descriptor->u.Memory.Length != 0 &&
                     DeviceExtension->MmioLength == 0)
            {
                DeviceExtension->MmioStart = Descriptor->u.Memory.Start;
                DeviceExtension->MmioLength = Descriptor->u.Memory.Length;
            }
        }
    }

    if (DeviceExtension->IoBase == NULL)
    {
        DPRINT1("SMBus: no I/O port BAR found\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Make sure firmware actually enabled the controller before touching it. */
    if (!SmbusEnableHostController(DeviceExtension))
    {
        DPRINT1("SMBus: failed to enable host controller\n");
        SmbusReleaseHardware(DeviceExtension);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = SmbusRecoverController(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        SmbusRestoreHostController(DeviceExtension);
        SmbusReleaseHardware(DeviceExtension);
        return Status;
    }

    SmbusDetectCapabilities(DeviceExtension);

    DeviceExtension->Started = TRUE;

    DPRINT1("SMBus: controller started at I/O port 0x%Ix length 0x%lx, "
            "MMIO 0x%I64x length 0x%lx, caps 0x%lx\n",
            (ULONG_PTR)DeviceExtension->IoStart.QuadPart,
            DeviceExtension->IoLength,
            (ULONGLONG)DeviceExtension->MmioStart.QuadPart,
            DeviceExtension->MmioLength,
            DeviceExtension->Capabilities);

#if SMBUS_TEMP_BOOT_PROBE
    SmbusDebugDumpController(DeviceExtension, "post-start");
    SmbusDebugProbeBus(DeviceExtension);
    SmbusDebugDumpController(DeviceExtension, "post-probe");
#endif

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SmbusCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMBUS_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return SmbusCompleteIrp(Irp, Status, 0);

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return SmbusCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NTAPI
SmbusDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMBUS_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack;
    PVOID Buffer;
    NTSTATUS Status;
    ULONG_PTR Information = 0;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return SmbusCompleteIrp(Irp, Status, 0);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    Buffer = Irp->AssociatedIrp.SystemBuffer;

    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_SMBUS_GET_CONTROLLER_INFO:
        {
            PSMBUS_CONTROLLER_INFO Info = Buffer;

            if (Info == NULL ||
                IrpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Info))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlZeroMemory(Info, sizeof(*Info));
            Info->Version = 1;
            Info->IoPort = DeviceExtension->IoStart.QuadPart;
            Info->IoLength = DeviceExtension->IoLength;
            Info->MmioPhysical = DeviceExtension->MmioStart.QuadPart;
            Info->MmioLength = DeviceExtension->MmioLength;
            Info->Capabilities = DeviceExtension->Capabilities;
            Info->Started = DeviceExtension->Started;
            if (DeviceExtension->Started)
            {
                ExAcquireFastMutex(&DeviceExtension->TransactionLock);
                Info->HostStatus = SmbusReadByte(DeviceExtension, SMBHSTSTS);
                SmbusReleaseHostSemaphore(DeviceExtension);
                ExReleaseFastMutex(&DeviceExtension->TransactionLock);
            }
            else
            {
                Info->HostStatus = 0;
            }

            Information = sizeof(*Info);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_SMBUS_EXECUTE:
        {
            PSMBUS_TRANSACTION Transaction = Buffer;

            if (Transaction == NULL ||
                IrpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Transaction) ||
                IrpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Transaction))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Status = SmbusExecuteTransaction(DeviceExtension, Transaction);
            Information = NT_SUCCESS(Status) ? sizeof(*Transaction) : 0;
            break;
        }

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return SmbusCompleteIrp(Irp, Status, Information);
}

NTSTATUS
NTAPI
SmbusPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMBUS_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return SmbusCompleteIrp(Irp, Status, 0);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = SmbusForwardIrpSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
            {
                /* The lower stack is running now, so PCI config is reachable. */
                Status = SmbusAcquireBusInterface(DeviceExtension);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("SMBus: failed to acquire PCI bus interface 0x%lx\n",
                            Status);
                }
                if (NT_SUCCESS(Status))
                {
                    Status = SmbusStartHardware(
                        DeviceExtension,
                        IrpStack->Parameters.StartDevice.AllocatedResources,
                        IrpStack->Parameters.StartDevice.AllocatedResourcesTranslated);
                    if (!NT_SUCCESS(Status))
                    {
                        DPRINT1("SMBus: failed to start hardware 0x%lx\n",
                                Status);
                    }
                }

                if (NT_SUCCESS(Status) &&
                    DeviceExtension->InterfaceRegistered &&
                    !DeviceExtension->InterfaceEnabled)
                {
                    NTSTATUS InterfaceStatus;

                    InterfaceStatus = IoSetDeviceInterfaceState(
                        &DeviceExtension->InterfaceName, TRUE);
                    if (NT_SUCCESS(InterfaceStatus))
                    {
                        DeviceExtension->InterfaceEnabled = TRUE;
                    }
                    else
                    {
                        DPRINT1("SMBus: failed to enable device interface 0x%lx\n",
                                InterfaceStatus);
                    }
                }

                if (!NT_SUCCESS(Status))
                {
                    SmbusRestoreHostController(DeviceExtension);
                    SmbusReleaseHardware(DeviceExtension);
                    SmbusReleaseBusInterface(DeviceExtension);
                }
            }
            else
            {
                DPRINT1("SMBus: lower stack failed start device 0x%lx\n",
                        Status);
            }

            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return SmbusCompleteIrp(Irp, Status, 0);

        case IRP_MN_STOP_DEVICE:
            if (DeviceExtension->InterfaceEnabled)
            {
                IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
                DeviceExtension->InterfaceEnabled = FALSE;
            }
            SmbusRestoreHostController(DeviceExtension);
            SmbusReleaseHardware(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return IoCallDriver(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_SURPRISE_REMOVAL:
            if (DeviceExtension->InterfaceEnabled)
            {
                IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
                DeviceExtension->InterfaceEnabled = FALSE;
            }
            SmbusReleaseHardware(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return IoCallDriver(DeviceExtension->LowerDevice, Irp);

        case IRP_MN_REMOVE_DEVICE:
            if (DeviceExtension->InterfaceEnabled)
            {
                IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
                DeviceExtension->InterfaceEnabled = FALSE;
            }
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            SmbusReleaseHardware(DeviceExtension);
            SmbusRestoreHostController(DeviceExtension);
            SmbusReleaseBusInterface(DeviceExtension);
            if (DeviceExtension->InterfaceRegistered)
            {
                RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
                DeviceExtension->InterfaceRegistered = FALSE;
            }
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return IoCallDriver(DeviceExtension->LowerDevice, Irp);
    }
}

NTSTATUS
NTAPI
SmbusPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMBUS_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

NTSTATUS
NTAPI
SmbusPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSMBUS_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

NTSTATUS
NTAPI
SmbusAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PSMBUS_DEVICE_EXTENSION DeviceExtension;

    Status = IoCreateDevice(DriverObject,
                            sizeof(SMBUS_DEVICE_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    DeviceExtension->Self = DeviceObject;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, SMBUS_TAG, 0, 0);
    ExInitializeFastMutex(&DeviceExtension->TransactionLock);

    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject,
                                                              PhysicalDeviceObject);
    if (DeviceExtension->LowerDevice == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    /* Best-effort: expose a device interface so consumers can find us. */
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject,
                                       &GUID_DEVINTERFACE_SMBUS,
                                       NULL,
                                       &DeviceExtension->InterfaceName);
    if (NT_SUCCESS(Status))
        DeviceExtension->InterfaceRegistered = TRUE;
    else
        DPRINT1("SMBus: IoRegisterDeviceInterface failed 0x%lx\n", Status);

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DPRINT1("SMBus: attached controller FDO\n");
    return STATUS_SUCCESS;
}

VOID
NTAPI
SmbusUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG MajorFunction;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (MajorFunction = 0; MajorFunction <= IRP_MJ_MAXIMUM_FUNCTION; MajorFunction++)
        DriverObject->MajorFunction[MajorFunction] = SmbusPassThrough;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = SmbusCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SmbusCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SmbusDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = SmbusPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = SmbusPower;
    DriverObject->DriverExtension->AddDevice = SmbusAddDevice;
    DriverObject->DriverUnload = SmbusUnload;

    return STATUS_SUCCESS;
}
