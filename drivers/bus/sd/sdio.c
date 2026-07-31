/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SDIO CMD52/CMD53 wrappers, CCCR/CIS parsing, and function enumeration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

#define CISTPL_NULL                 0x00
#define CISTPL_CHECKSUM             0x10
#define CISTPL_VERS_1               0x15
#define CISTPL_ALTSTR               0x16
#define CISTPL_MANFID               0x20
#define CISTPL_FUNCID               0x21
#define CISTPL_FUNCE                0x22
#define CISTPL_SDIO_STD             0x91
#define CISTPL_SDIO_EXT             0x92
#define CISTPL_END                  0xFF

#define SDIO_CIS_WALK_MAX_BYTES     4096

#define CMD52_ARG_W_SHIFT           31
#define CMD52_ARG_FUNC_SHIFT        28
#define CMD52_ARG_FUNC_MASK         0x7
#define CMD52_ARG_RAW_SHIFT         27
#define CMD52_ARG_ADDR_SHIFT        9
#define CMD52_ARG_ADDR_MASK         0x1FFFFUL
#define CMD52_ARG_DATA_MASK         0xFFUL

#define CMD53_ARG_W_SHIFT           31
#define CMD53_ARG_FUNC_SHIFT        28
#define CMD53_ARG_FUNC_MASK         0x7
#define CMD53_ARG_BLOCK_SHIFT       27
#define CMD53_ARG_OP_SHIFT          26
#define CMD53_ARG_ADDR_SHIFT        9
#define CMD53_ARG_ADDR_MASK         0x1FFFFUL
#define CMD53_ARG_COUNT_MASK        0x1FFUL

#define SDIO_R5_COM_CRC_ERROR       (1UL << 15)
#define SDIO_R5_ILLEGAL_COMMAND     (1UL << 14)
#define SDIO_R5_ERROR               (1UL << 11)
#define SDIO_R5_FUNCTION_NUMBER     (1UL << 9)
#define SDIO_R5_OUT_OF_RANGE        (1UL << 8)

#define SDIO_MAX_FUNCTIONS          7
#define SDIO_CCCR_REV_MASK          0x0F
#define SDIO_CCCR_REV_1_20          2
#define SDIO_CCCR_REV_3_00          3
#define SDIO_CCCR_CAP_LSC           0x40
#define SDIO_CCCR_CAP_4BLS          0x80
#define SDIO_SPEED_SHS              0x01
#define SDIO_SPEED_BSS_MASK         0x0E
#define SDIO_SPEED_EHS              0x02

typedef struct _SDBUS_SDIO_FUNCTION_INFO
{
    UCHAR StandardInterface;
    UCHAR ExtendedInterface;
    UCHAR FunctionClass;
    ULONG CisPointer;
    USHORT VendorId;
    USHORT DeviceId;
} SDBUS_SDIO_FUNCTION_INFO, *PSDBUS_SDIO_FUNCTION_INFO;

NTSTATUS
SdBusSdioR5Status(
    _In_ ULONG Response)
{
    if (Response & SDIO_R5_COM_CRC_ERROR)
    {
        return STATUS_SD_CMD_CRC_ERROR;
    }
    if (Response & SDIO_R5_ILLEGAL_COMMAND)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (Response & (SDIO_R5_FUNCTION_NUMBER | SDIO_R5_OUT_OF_RANGE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Response & SDIO_R5_ERROR)
    {
        return STATUS_IO_DEVICE_ERROR;
    }

    return STATUS_SUCCESS;
}

static __inline ULONG
SdBusSdioBuildCmd52Argument(
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN RawMode,
    _In_ ULONG Address,
    _In_ UCHAR DataIn)
{
    ULONG Arg = 0;

    if (Write)
    {
        Arg |= (1UL << CMD52_ARG_W_SHIFT);
    }

    Arg |= ((ULONG)(Function & CMD52_ARG_FUNC_MASK)) << CMD52_ARG_FUNC_SHIFT;

    if (RawMode && Write)
    {
        Arg |= (1UL << CMD52_ARG_RAW_SHIFT);
    }

    Arg |= ((Address & CMD52_ARG_ADDR_MASK) << CMD52_ARG_ADDR_SHIFT);
    Arg |= ((ULONG)DataIn & CMD52_ARG_DATA_MASK);

    return Arg;
}

static __inline ULONG
SdBusSdioBuildCmd53Argument(
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN BlockMode,
    _In_ BOOLEAN Increment,
    _In_ ULONG Address,
    _In_ ULONG Count)
{
    ULONG Arg = 0;
    ULONG EncodedCount;

    if (Write)
    {
        Arg |= (1UL << CMD53_ARG_W_SHIFT);
    }

    Arg |= ((ULONG)(Function & CMD53_ARG_FUNC_MASK)) << CMD53_ARG_FUNC_SHIFT;

    if (BlockMode)
    {
        Arg |= (1UL << CMD53_ARG_BLOCK_SHIFT);
    }

    if (Increment)
    {
        Arg |= (1UL << CMD53_ARG_OP_SHIFT);
    }

    Arg |= ((Address & CMD53_ARG_ADDR_MASK) << CMD53_ARG_ADDR_SHIFT);

    EncodedCount = Count & CMD53_ARG_COUNT_MASK;
    Arg |= EncodedCount;

    return Arg;
}

NTSTATUS
SdBusSdioCmd52(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN RawMode,
    _In_ ULONG Address,
    _In_ UCHAR DataIn,
    _Out_opt_ PUCHAR DataOut)
{
    NTSTATUS Status;
    ULONG Argument;
    ULONG Response = 0;

    if (FdoExtension == NULL || Function > CMD52_ARG_FUNC_MASK ||
        Address > CMD52_ARG_ADDR_MASK || (RawMode && !Write))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Argument = SdBusSdioBuildCmd52Argument(Function, Write, RawMode,
                                           Address, DataIn);

    Status = SdBusSendCommand(FdoExtension,
                              SDCMD_IO_RW_DIRECT,
                              Argument,
                              SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                                  SDHCI_CMD_INDEX_CHECK,
                              &Response);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SdBusSdioR5Status(Response);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (DataOut != NULL)
    {
        *DataOut = (UCHAR)(Response & 0xFF);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
SdBusSetSdioFunctionEnabled(
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ BOOLEAN Enable)
{
    PFDO_EXTENSION FdoExtension;
    UCHAR FunctionBit;
    UCHAR IoEnable;
    UCHAR RequestedEnable;
    UCHAR DataOut;
    ULONG Timeout;
    LARGE_INTEGER Delay;
    NTSTATUS Status;

    if (PdoExtension == NULL || PdoExtension->FunctionNumber == 0 ||
        PdoExtension->FunctionNumber > PdoExtension->SdioNumFunctions ||
        PdoExtension->FunctionNumber > SDIO_MAX_FUNCTIONS ||
        (PdoExtension->CardType != SdCardTypeSdio &&
         PdoExtension->CardType != SdCardTypeCombo))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!PdoExtension->Present)
    {
        return STATUS_SD_CARD_REMOVED;
    }
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    FdoExtension = PdoExtension->FdoExtension;
    if (FdoExtension == NULL || FdoExtension->RegisterBase == NULL ||
        FdoExtension->Common.DeviceState != SdBusDeviceStateStarted)
    {
        return STATUS_DEVICE_NOT_READY;
    }
    FunctionBit = (UCHAR)(1U << PdoExtension->FunctionNumber);
    Delay.QuadPart = -10000LL;

    ExAcquireFastMutex(&FdoExtension->CommandMutex);

    if (!PdoExtension->Present || FdoExtension->RegisterBase == NULL ||
        FdoExtension->Common.DeviceState != SdBusDeviceStateStarted ||
        (Enable &&
         (!PdoExtension->Started ||
          InterlockedCompareExchange(&PdoExtension->ManageIoEnable, 0, 0) == 0)))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                            SDIO_CCCR_IO_ENABLE, 0, &IoEnable);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    RequestedEnable = Enable ? (IoEnable | FunctionBit) :
                               (IoEnable & ~FunctionBit);
    if (RequestedEnable != IoEnable)
    {
        Status = SdBusSdioCmd52(FdoExtension, 0, TRUE, TRUE,
                                SDIO_CCCR_IO_ENABLE, RequestedEnable,
                                &DataOut);
        if (!NT_SUCCESS(Status))
        {
            goto Exit;
        }
        if (DataOut != RequestedEnable)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
            goto Exit;
        }
    }

    if (!Enable)
    {
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    for (Timeout = 0; Timeout < 1000; Timeout++)
    {
        Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                SDIO_CCCR_IO_READY, 0, &DataOut);
        if (!NT_SUCCESS(Status))
        {
            goto Rollback;
        }
        if (DataOut & FunctionBit)
        {
            Status = STATUS_SUCCESS;
            goto Exit;
        }
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    Status = STATUS_IO_TIMEOUT;

Rollback:
    RequestedEnable &= ~FunctionBit;
    if (!NT_SUCCESS(SdBusSdioCmd52(FdoExtension, 0, TRUE, TRUE,
                                   SDIO_CCCR_IO_ENABLE, RequestedEnable,
                                   &DataOut)) ||
        DataOut != RequestedEnable)
    {
        DPRINT1("SdBusSetSdioFunctionEnabled: function %lu rollback failed\n",
                PdoExtension->FunctionNumber);
        Status = STATUS_DEVICE_DATA_ERROR;
    }

Exit:
    ExReleaseFastMutex(&FdoExtension->CommandMutex);
    InterlockedExchange(&PdoExtension->SdioEnableStatus, Status);
    InterlockedExchange(&PdoExtension->SdioEnablePending, 0);
    if (NT_SUCCESS(Status))
    {
        InterlockedExchange(&PdoExtension->SdioFunctionEnabled,
                            Enable ? 1 : 0);
    }
    return Status;
}

NTSTATUS
SdBusSetSdioFunctionEnabledAdmitted(
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ BOOLEAN Enable)
{
    PFDO_EXTENSION FdoExtension;
    NTSTATUS Status;

    if (PdoExtension == NULL || PdoExtension->FdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FdoExtension = PdoExtension->FdoExtension;
    Status = SdBusAcquireRequestAdmission(FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SdBusSetSdioFunctionEnabled(PdoExtension, Enable);
    SdBusReleaseRequestAdmission(FdoExtension);
    return Status;
}

VOID
SdBusProcessPendingSdioEnables(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PDEVICE_OBJECT FunctionPdos[SDIO_MAX_FUNCTIONS];
    ULONG FunctionCount = 0;
    ULONG FunctionIndex;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (FdoExtension == NULL ||
        InterlockedCompareExchange(&FdoExtension->WorkerShutdown, 0, 0) != 0 ||
        InterlockedCompareExchange(&FdoExtension->RequestsBlocked, 0, 0) != 0 ||
        FdoExtension->Common.DeviceState != SdBusDeviceStateStarted)
    {
        return;
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    for (Entry = FdoExtension->ChildPdoList.Flink;
         Entry != &FdoExtension->ChildPdoList &&
         FunctionCount < RTL_NUMBER_OF(FunctionPdos);
         Entry = Entry->Flink)
    {
        PPDO_EXTENSION PdoExtension;

        PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
        if (PdoExtension->Present && PdoExtension->Started &&
            PdoExtension->FunctionNumber != 0 &&
            InterlockedCompareExchange(&PdoExtension->ManageIoEnable, 0, 0) != 0 &&
            InterlockedCompareExchange(&PdoExtension->SdioEnablePending, 0, 0) != 0)
        {
            FunctionPdos[FunctionCount] = PdoExtension->Common.Self;
            ObReferenceObject(FunctionPdos[FunctionCount]);
            FunctionCount++;
        }
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    for (FunctionIndex = 0; FunctionIndex < FunctionCount; FunctionIndex++)
    {
        PPDO_EXTENSION PdoExtension;
        NTSTATUS Status;

        PdoExtension = (PPDO_EXTENSION)FunctionPdos[FunctionIndex]->DeviceExtension;
        if (PdoExtension->Present && PdoExtension->Started &&
            InterlockedCompareExchange(&PdoExtension->ManageIoEnable, 0, 0) != 0 &&
            InterlockedCompareExchange(&PdoExtension->SdioEnablePending, 0, 0) != 0)
        {
            Status = SdBusAcquireRequestAdmission(FdoExtension);
            if (NT_SUCCESS(Status))
            {
                if (InterlockedCompareExchange(&PdoExtension->SdioEnablePending,
                                               0,
                                               1) == 1)
                {
                    Status = SdBusSetSdioFunctionEnabled(PdoExtension, TRUE);
                }
                SdBusReleaseRequestAdmission(FdoExtension);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("SdBusProcessPendingSdioEnables: function %lu failed "
                            "(0x%08lx)\n", PdoExtension->FunctionNumber, Status);
                }
            }
        }
        ObDereferenceObject(FunctionPdos[FunctionIndex]);
    }
}

NTSTATUS
SdBusSdioReadCccr(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Address,
    _Out_ PUCHAR DataOut)
{
    if (DataOut == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE, Address, 0, DataOut);
}

NTSTATUS
SdBusSdioWriteCccr(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Address,
    _In_ UCHAR Data)
{
    return SdBusSdioCmd52(FdoExtension, 0, TRUE, FALSE, Address, Data, NULL);
}

NTSTATUS
SdBusSdioCmd53(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION PdoExtension,
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN BlockMode,
    _In_ BOOLEAN Increment,
    _In_ ULONG Address,
    _In_ ULONG Count,
    _In_ ULONG BlockSize,
    _Inout_opt_ PMDL Mdl)
{
    SDCMD_DESCRIPTOR CmdDesc;
    ULONG Argument;
    ULONG DataLength;
    ULONG ActualCount;
    ULONG TransferBlockSize;
    ULONG Response = 0;

    if (FdoExtension == NULL || PdoExtension == NULL ||
        PdoExtension->FdoExtension != FdoExtension ||
        Function > CMD53_ARG_FUNC_MASK || Address > CMD53_ARG_ADDR_MASK)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((PdoExtension->CardType != SdCardTypeSdio &&
         PdoExtension->CardType != SdCardTypeCombo) ||
        Function > PdoExtension->SdioNumFunctions)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Count > 512)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ActualCount = (Count == 0) ? 512 : Count;

    if (Mdl == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (BlockMode)
    {
        if (BlockSize == 0 || BlockSize > SDHCI_BLOCK_SIZE_MASK)
        {
            return STATUS_INVALID_PARAMETER;
        }
        TransferBlockSize = BlockSize;
        DataLength = ActualCount * BlockSize;
    }
    else
    {
        if (BlockSize != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        TransferBlockSize = ActualCount;
        DataLength = ActualCount;
    }

    if (MmGetMdlByteCount(Mdl) < DataLength ||
        (Increment && DataLength - 1 > CMD53_ARG_ADDR_MASK - Address))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&CmdDesc, sizeof(CmdDesc));
    CmdDesc.Cmd = SDCMD_IO_RW_EXTENDED;
    CmdDesc.CmdClass = SDCC_STANDARD;
    CmdDesc.TransferDirection = Write ? SDTD_WRITE : SDTD_READ;
    CmdDesc.TransferType = (BlockMode && ActualCount > 1) ?
        SDTT_MULTI_BLOCK_NO_CMD12 : SDTT_SINGLE_BLOCK;
    CmdDesc.ResponseType = SDRT_5;

    Argument = SdBusSdioBuildCmd53Argument(Function, Write, BlockMode,
                                            Increment, Address, Count);

    {
        NTSTATUS Status;

        Status = SdBusSendSdhciCommand(FdoExtension, &CmdDesc, Argument,
                                       Mdl, DataLength, TransferBlockSize, 0,
                                       &Response);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        return SdBusSdioR5Status(Response);
    }
}

static NTSTATUS
SdBusSdioReadCisPointer(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR Function,
    _In_ ULONG BaseAddress,
    _Out_ PULONG OutPointer)
{
    NTSTATUS Status;
    UCHAR Byte0 = 0;
    UCHAR Byte1 = 0;
    UCHAR Byte2 = 0;

    if (OutPointer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (BaseAddress > CMD52_ARG_ADDR_MASK - 2)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                            BaseAddress, 0, &Byte0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                            BaseAddress + 1, 0, &Byte1);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                            BaseAddress + 2, 0, &Byte2);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    UNREFERENCED_PARAMETER(Function);

    *OutPointer = ((ULONG)Byte0) |
                  (((ULONG)Byte1) << 8) |
                  (((ULONG)Byte2) << 16);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusSdioWalkCis(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG CisPointer,
    _Out_opt_ PUSHORT OutVid,
    _Out_opt_ PUSHORT OutDid,
    _Out_opt_ PUCHAR OutFuncClass)
{
    NTSTATUS Status;
    ULONG Address = CisPointer;
    ULONG BytesWalked = 0;
    UCHAR TupleCode;
    UCHAR TupleLink;
    ULONG BodyStart;

    if (OutVid != NULL)
    {
        *OutVid = 0;
    }
    if (OutDid != NULL)
    {
        *OutDid = 0;
    }
    if (OutFuncClass != NULL)
    {
        *OutFuncClass = 0;
    }

    if (CisPointer == 0)
    {
        return STATUS_NOT_FOUND;
    }
    if (CisPointer > CMD52_ARG_ADDR_MASK)
    {
        return STATUS_INVALID_PARAMETER;
    }

    while (BytesWalked < SDIO_CIS_WALK_MAX_BYTES)
    {
        if (Address > CMD52_ARG_ADDR_MASK)
        {
            return STATUS_DEVICE_DATA_ERROR;
        }

        Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                Address, 0, &TupleCode);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (TupleCode == CISTPL_END)
        {
            return STATUS_SUCCESS;
        }

        if (TupleCode == CISTPL_NULL)
        {
            Address += 1;
            BytesWalked += 1;
            continue;
        }

        if (Address == CMD52_ARG_ADDR_MASK)
        {
            return STATUS_DEVICE_DATA_ERROR;
        }

        Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                Address + 1, 0, &TupleLink);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        BodyStart = Address + 2;
        if (BodyStart > CMD52_ARG_ADDR_MASK ||
            TupleLink > CMD52_ARG_ADDR_MASK - BodyStart)
        {
            return STATUS_DEVICE_DATA_ERROR;
        }

        switch (TupleCode)
        {
            case CISTPL_MANFID:
            {
                UCHAR VidLo = 0, VidHi = 0, DidLo = 0, DidHi = 0;

                if (TupleLink < 4)
                {
                    break;
                }
                Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                        BodyStart + 0, 0, &VidLo);
                if (NT_SUCCESS(Status))
                    Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                            BodyStart + 1, 0, &VidHi);
                if (NT_SUCCESS(Status))
                    Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                            BodyStart + 2, 0, &DidLo);
                if (NT_SUCCESS(Status))
                    Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                            BodyStart + 3, 0, &DidHi);
                if (!NT_SUCCESS(Status))
                {
                    return Status;
                }

                if (OutVid != NULL)
                {
                    *OutVid = (USHORT)(((USHORT)VidHi << 8) | VidLo);
                }
                if (OutDid != NULL)
                {
                    *OutDid = (USHORT)(((USHORT)DidHi << 8) | DidLo);
                }
                break;
            }

            case CISTPL_FUNCID:
            {
                UCHAR Class = 0;
                if (TupleLink < 1)
                {
                    break;
                }
                Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                        BodyStart + 0, 0, &Class);
                if (!NT_SUCCESS(Status))
                {
                    return Status;
                }
                if (OutFuncClass != NULL)
                {
                    *OutFuncClass = Class;
                }
                break;
            }

            default:
                break;
        }

        Address = BodyStart + TupleLink;
        BytesWalked += (2 + TupleLink);
    }

    DPRINT1("SdBusSdioWalkCis: CIS walk exceeded maximum length, aborting\n");
    return STATUS_TIMEOUT;
}

static NTSTATUS
SdBusCreateSdioFunctionPdo(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION HostPdo,
    _In_ UCHAR FunctionNumber,
    _In_ USHORT Vid,
    _In_ USHORT Did,
    _In_ UCHAR Class,
    _Out_ PDEVICE_OBJECT *CreatedPdo)
{
    PDEVICE_OBJECT Pdo;
    PPDO_EXTENSION PdoExtension;
    NTSTATUS Status;

    if (FdoExtension == NULL || HostPdo == NULL || CreatedPdo == NULL ||
        FunctionNumber == 0 || FunctionNumber > SDIO_MAX_FUNCTIONS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *CreatedPdo = NULL;

    Status = IoCreateDevice(FdoExtension->Common.Self->DriverObject,
                            sizeof(PDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_AUTOGENERATED_DEVICE_NAME,
                            FALSE,
                            &Pdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusCreateSdioFunctionPdo: IoCreateDevice failed (0x%08lx)\n",
                Status);
        return Status;
    }

    PdoExtension = (PPDO_EXTENSION)Pdo->DeviceExtension;
    RtlZeroMemory(PdoExtension, sizeof(PDO_EXTENSION));

    PdoExtension->Common.Self = Pdo;
    PdoExtension->Common.IsFdo = FALSE;
    PdoExtension->Common.DeviceState = SdBusDeviceStateStopped;
    PdoExtension->Common.DevicePowerState = PowerDeviceD0;

    PdoExtension->FdoExtension = FdoExtension;
    PdoExtension->Present = TRUE;
    PdoExtension->ReportedMissing = FALSE;
    PdoExtension->Started = FALSE;
    PdoExtension->CardType = HostPdo->CardType;
    PdoExtension->RelativeAddress = HostPdo->RelativeAddress;
    RtlCopyMemory(PdoExtension->Scr, HostPdo->Scr,
                  sizeof(PdoExtension->Scr));
    PdoExtension->FunctionNumber = FunctionNumber;
    PdoExtension->SdioNumFunctions = HostPdo->SdioNumFunctions;
    PdoExtension->SdioCccrRev = HostPdo->SdioCccrRev;
    PdoExtension->SdioSdSpecRev = HostPdo->SdioSdSpecRev;
    PdoExtension->SdioCardCap = HostPdo->SdioCardCap;
    PdoExtension->SdioBusIfCtrl = HostPdo->SdioBusIfCtrl;
    PdoExtension->SdioUhsSupport = HostPdo->SdioUhsSupport;
    PdoExtension->SdioCommonCisPointer = HostPdo->SdioCommonCisPointer;
    PdoExtension->SdioVendorId = Vid;
    PdoExtension->SdioDeviceId = Did;
    PdoExtension->SdioClass = Class;

    PdoExtension->InsertionGeneration = HostPdo->InsertionGeneration;

    InitializeListHead(&PdoExtension->ListEntry);
    IoInitializeRemoveLock(&PdoExtension->RemoveLock, TAG_SDBUS, 0, 0);

    Pdo->Flags |= DO_DIRECT_IO;
    if (FdoExtension->Common.PowerPagable)
    {
        Pdo->Flags |= DO_POWER_PAGABLE;
    }
    SdBusInitializeDeviceUsage(&PdoExtension->Common,
                               FdoExtension->Common.PowerPagable);
    *CreatedPdo = Pdo;

    return STATUS_SUCCESS;
}

VOID
SdBusDeleteMissingInternalSdioHosts(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PDEVICE_OBJECT DeviceObject;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (FdoExtension == NULL)
    {
        return;
    }

    for (;;)
    {
        DeviceObject = NULL;

        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
        for (Entry = FdoExtension->ChildPdoList.Flink;
             Entry != &FdoExtension->ChildPdoList;
             Entry = Entry->Flink)
        {
            PPDO_EXTENSION PdoExtension;

            PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
            if (PdoExtension->IsInternalSdioHost && !PdoExtension->Present)
            {
                DeviceObject = PdoExtension->Common.Self;
                RemoveEntryList(&PdoExtension->ListEntry);
                if (FdoExtension->ChildPdoCount != 0)
                {
                    FdoExtension->ChildPdoCount--;
                }
                break;
            }
        }
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

        if (DeviceObject == NULL)
        {
            break;
        }

        IoDeleteDevice(DeviceObject);
    }
}

static NTSTATUS
SdBusReuseSdioFunctionPdos(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION HostPdo,
    _In_reads_(NumFunctions) PSDBUS_SDIO_FUNCTION_INFO FunctionInfo,
    _In_ ULONG NumFunctions,
    _Out_ PBOOLEAN Reused)
{
    PLIST_ENTRY Entry;
    PPDO_EXTENSION FunctionPdo;
    ULONG FunctionCount = 0;
    UCHAR SeenFunctions = 0;
    UCHAR ExpectedFunctions;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    *Reused = FALSE;
    ExpectedFunctions = (UCHAR)(((1UL << NumFunctions) - 1) << 1);

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    for (Entry = FdoExtension->ChildPdoList.Flink;
         Entry != &FdoExtension->ChildPdoList;
         Entry = Entry->Flink)
    {
        ULONG Index;
        UCHAR FunctionBit;
        PSDBUS_SDIO_FUNCTION_INFO Info;

        FunctionPdo = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
        if (FunctionPdo->InsertionGeneration != HostPdo->InsertionGeneration ||
            FunctionPdo->FunctionNumber == 0)
        {
            continue;
        }

        if (!FunctionPdo->Present ||
            FunctionPdo->FunctionNumber > NumFunctions)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
            break;
        }

        FunctionBit = (UCHAR)(1U << FunctionPdo->FunctionNumber);
        if (SeenFunctions & FunctionBit)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
            break;
        }

        Index = FunctionPdo->FunctionNumber - 1;
        Info = &FunctionInfo[Index];
        if (FunctionPdo->SdioVendorId != Info->VendorId ||
            FunctionPdo->SdioDeviceId != Info->DeviceId ||
            FunctionPdo->SdioClass != Info->FunctionClass)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
            break;
        }

        SeenFunctions |= FunctionBit;
        FunctionCount++;
    }

    if (NT_SUCCESS(Status) && FunctionCount != 0)
    {
        if (FunctionCount != NumFunctions || SeenFunctions != ExpectedFunctions)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
        }
        else
        {
            for (Entry = FdoExtension->ChildPdoList.Flink;
                 Entry != &FdoExtension->ChildPdoList;
                 Entry = Entry->Flink)
            {
                FunctionPdo = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
                if (FunctionPdo->InsertionGeneration ==
                        HostPdo->InsertionGeneration &&
                    FunctionPdo->FunctionNumber != 0)
                {
                    FunctionPdo->CardType = HostPdo->CardType;
                    FunctionPdo->RelativeAddress = HostPdo->RelativeAddress;
                    RtlCopyMemory(FunctionPdo->Scr, HostPdo->Scr,
                                  sizeof(FunctionPdo->Scr));
                    FunctionPdo->SdioNumFunctions = (UCHAR)NumFunctions;
                    FunctionPdo->SdioCccrRev = HostPdo->SdioCccrRev;
                    FunctionPdo->SdioSdSpecRev = HostPdo->SdioSdSpecRev;
                    FunctionPdo->SdioCardCap = HostPdo->SdioCardCap;
                    FunctionPdo->SdioBusIfCtrl = HostPdo->SdioBusIfCtrl;
                    FunctionPdo->SdioUhsSupport = HostPdo->SdioUhsSupport;
                    FunctionPdo->SdioCommonCisPointer =
                        HostPdo->SdioCommonCisPointer;
                }
            }
            *Reused = TRUE;
        }
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    return Status;
}

NTSTATUS
SdBusSdioEnumerateFunctions(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION HostPdo,
    _In_ ULONG NumFunctions)
{
    NTSTATUS Status;
    NTSTATUS RollbackStatus;
    UCHAR CccrRev = 0;
    UCHAR SdSpecRev = 0;
    UCHAR CardCap = 0;
    UCHAR BusIfCtrl = 0;
    UCHAR UhsSupport = 0;
    ULONG CommonCisPointer = 0;
    USHORT CommonVid = 0;
    USHORT CommonDid = 0;
    UCHAR CommonClass = 0;
    ULONG FunctionIndex;
    ULONG CreatedCount = 0;
    SDBUS_SDIO_FUNCTION_INFO FunctionInfo[SDIO_MAX_FUNCTIONS];
    PDEVICE_OBJECT FunctionPdos[SDIO_MAX_FUNCTIONS];
    KIRQL OldIrql;
    BOOLEAN Reused;

    if (FdoExtension == NULL || HostPdo == NULL ||
        HostPdo->FdoExtension != FdoExtension ||
        (HostPdo->CardType != SdCardTypeSdio &&
         HostPdo->CardType != SdCardTypeCombo))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (NumFunctions == 0)
    {
        DPRINT1("SdBusSdioEnumerateFunctions: No SDIO functions to enumerate\n");
        return STATUS_SUCCESS;
    }

    if (NumFunctions > SDIO_MAX_FUNCTIONS)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    RtlZeroMemory(FunctionInfo, sizeof(FunctionInfo));
    RtlZeroMemory(FunctionPdos, sizeof(FunctionPdos));

    Status = SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_REVISION, &CccrRev);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((CccrRev & SDIO_CCCR_REV_MASK) > SDIO_CCCR_REV_3_00)
    {
        DPRINT1("SdBusSdioEnumerateFunctions: unsupported CCCR revision 0x%02X\n",
                CccrRev);
        return STATUS_NOT_SUPPORTED;
    }

    Status = SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_SD_SPEC, &SdSpecRev);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_CARD_CAPABILITY, &CardCap);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_BUS_INTERFACE, &BusIfCtrl);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((CccrRev & SDIO_CCCR_REV_MASK) >= SDIO_CCCR_REV_3_00)
    {
        Status = SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_UHS_SUPPORT,
                                    &UhsSupport);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = SdBusSdioReadCisPointer(FdoExtension, 0, SDIO_CCCR_CIS_POINTER,
                                     &CommonCisPointer);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusSdioEnumerateFunctions: common CIS pointer read failed "
                "(0x%08lx)\n", Status);
        return Status;
    }

    DPRINT1("SdBusSdioEnumerateFunctions: CCCR rev=0x%02X spec=0x%02X cap=0x%02X "
            "bus_if=0x%02X uhs=0x%02X CIS=0x%06lX NumFuncs=%lu\n",
            CccrRev, SdSpecRev, CardCap, BusIfCtrl, UhsSupport,
            CommonCisPointer, NumFunctions);

    Status = SdBusSdioWalkCis(FdoExtension, CommonCisPointer,
                              &CommonVid, &CommonDid, &CommonClass);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusSdioEnumerateFunctions: common CIS walk failed "
                "(0x%08lx)\n", Status);
        return Status;
    }
    DPRINT1("SdBusSdioEnumerateFunctions: Common CIS VID=0x%04X DID=0x%04X "
            "class=0x%02X\n", CommonVid, CommonDid, CommonClass);

    for (FunctionIndex = 1; FunctionIndex <= NumFunctions; FunctionIndex++)
    {
        ULONG FbrBase = SDIO_FBR_BASE(FunctionIndex);
        PSDBUS_SDIO_FUNCTION_INFO Info = &FunctionInfo[FunctionIndex - 1];
        USHORT FuncVid = CommonVid;
        USHORT FuncDid = CommonDid;
        UCHAR TupleClass = 0;

        Status = SdBusSdioReadCccr(FdoExtension,
                                   FbrBase + SDIO_FBR_STD_INTERFACE,
                                   &Info->StandardInterface);
        if (!NT_SUCCESS(Status))
            return Status;

        Info->FunctionClass = Info->StandardInterface & 0x0F;
        if (Info->FunctionClass == 0x0F)
        {
            Status = SdBusSdioReadCccr(FdoExtension,
                                       FbrBase + SDIO_FBR_EXT_INTERFACE,
                                       &Info->ExtendedInterface);
            if (!NT_SUCCESS(Status))
                return Status;
            Info->FunctionClass = Info->ExtendedInterface;
        }

        Status = SdBusSdioReadCisPointer(FdoExtension, (UCHAR)FunctionIndex,
                                         FbrBase + SDIO_FBR_CIS_POINTER,
                                         &Info->CisPointer);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusSdioEnumerateFunctions: func %lu CIS pointer read "
                    "failed (0x%08lx)\n", FunctionIndex, Status);
            return Status;
        }

        {
            USHORT TupleVid = 0;
            USHORT TupleDid = 0;

            Status = SdBusSdioWalkCis(FdoExtension, Info->CisPointer,
                                      &TupleVid, &TupleDid, &TupleClass);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SdBusSdioEnumerateFunctions: func %lu CIS walk failed "
                        "(0x%08lx)\n", FunctionIndex, Status);
                return Status;
            }

            if (TupleVid != 0)
                FuncVid = TupleVid;
            if (TupleDid != 0)
                FuncDid = TupleDid;
            if (TupleClass != 0 && Info->FunctionClass == 0)
                Info->FunctionClass = TupleClass;
        }

        if (FuncVid == 0 || FuncDid == 0)
        {
            DPRINT1("SdBusSdioEnumerateFunctions: func %lu has no usable "
                    "manufacturer/card ID\n", FunctionIndex);
            return STATUS_DEVICE_DATA_ERROR;
        }

        Info->VendorId = FuncVid;
        Info->DeviceId = FuncDid;

        DPRINT1("SdBusSdioEnumerateFunctions: func %lu std=0x%02X ext=0x%02X "
                "class=0x%02X CIS=0x%06lX VID=0x%04X DID=0x%04X\n",
                FunctionIndex, Info->StandardInterface,
                Info->ExtendedInterface, Info->FunctionClass,
                Info->CisPointer, Info->VendorId, Info->DeviceId);
    }

    HostPdo->SdioCccrRev = CccrRev;
    HostPdo->SdioSdSpecRev = SdSpecRev;
    HostPdo->SdioCardCap = CardCap;
    HostPdo->SdioBusIfCtrl = BusIfCtrl;
    HostPdo->SdioUhsSupport = UhsSupport;
    HostPdo->SdioCommonCisPointer = CommonCisPointer;
    HostPdo->SdioNumFunctions = (UCHAR)NumFunctions;
    HostPdo->SdioVendorId = CommonVid;
    HostPdo->SdioDeviceId = CommonDid;

    {
        UCHAR BusIf = (UCHAR)((BusIfCtrl & ~0x03) | 0x02);
        UCHAR HighSpeed = 0;
        UCHAR HostCtrl;
        BOOLEAN CanUseFourBit;

        HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
        HostCtrl &= ~SDHCI_HC_DATA_WIDTH_4BIT;
        SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
        FdoExtension->CurrentBusWidth = 1;

        CanUseFourBit = !(CardCap & SDIO_CCCR_CAP_LSC) ||
                        (CardCap & SDIO_CCCR_CAP_4BLS);
        if (HostPdo->CardType == SdCardTypeCombo &&
            !(HostPdo->Scr[1] & SD_SCR_BUS_WIDTH_4))
        {
            CanUseFourBit = FALSE;
        }

        if (CanUseFourBit)
        {
            Status = SdBusSdioWriteCccr(FdoExtension,
                                         SDIO_CCCR_BUS_INTERFACE,
                                         BusIf);
            if (NT_SUCCESS(Status) && HostPdo->CardType == SdCardTypeCombo)
            {
                Status = SdBusSendAppCommand(FdoExtension,
                                             HostPdo->RelativeAddress,
                                             SDACMD_SET_BUS_WIDTH,
                                             SD_ACMD6_BUS_WIDTH_4,
                                             SDHCI_CMD_RESP_48 |
                                                 SDHCI_CMD_CRC_CHECK |
                                                 SDHCI_CMD_INDEX_CHECK,
                                             NULL);
                if (!NT_SUCCESS(Status))
                {
                    RollbackStatus = SdBusSdioWriteCccr(
                        FdoExtension,
                        SDIO_CCCR_BUS_INTERFACE,
                        (UCHAR)(BusIfCtrl & ~0x03));
                    if (!NT_SUCCESS(RollbackStatus))
                    {
                        DPRINT1("SdBusSdioEnumerateFunctions: combo-card "
                                "4-bit rollback failed (0x%08lx)\n",
                                RollbackStatus);
                        return RollbackStatus;
                    }
                }
            }

            if (NT_SUCCESS(Status))
            {
                HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
                HostCtrl |= SDHCI_HC_DATA_WIDTH_4BIT;
                SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
                FdoExtension->CurrentBusWidth = 4;
                DPRINT1("SdBusSdioEnumerateFunctions: SDIO 4-bit bus enabled\n");
            }
            else
            {
                DPRINT1("SdBusSdioEnumerateFunctions: 4-bit switch failed "
                        "(0x%08lx), staying on 1-bit bus\n", Status);
            }
        }

        if ((CccrRev & SDIO_CCCR_REV_MASK) >= SDIO_CCCR_REV_1_20)
        {
            Status = SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_HIGH_SPEED,
                                       &HighSpeed);
            if (!NT_SUCCESS(Status))
                return Status;

            if ((HighSpeed & SDIO_SPEED_SHS) &&
                HostPdo->CardType == SdCardTypeSdio &&
                (FdoExtension->HostCapabilities & SDHCI_CAP_HIGH_SPEED))
            {
                HighSpeed = (UCHAR)((HighSpeed & ~SDIO_SPEED_BSS_MASK) |
                                    SDIO_SPEED_EHS);
                Status = SdBusSdioWriteCccr(FdoExtension,
                                             SDIO_CCCR_HIGH_SPEED,
                                             HighSpeed);
                if (NT_SUCCESS(Status))
                {
                    HostCtrl = SdBusReadReg8(FdoExtension,
                                             SDHCI_HOST_CONTROL);
                    HostCtrl |= SDHCI_HC_HIGH_SPEED;
                    SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
                    DPRINT1("SdBusSdioEnumerateFunctions: SDIO high-speed enabled\n");
                }
                else
                {
                    DPRINT1("SdBusSdioEnumerateFunctions: high-speed switch "
                            "failed (0x%08lx), staying at default speed\n",
                            Status);
                }
            }
        }
    }

    Status = SdBusReuseSdioFunctionPdos(FdoExtension, HostPdo,
                                        FunctionInfo, NumFunctions, &Reused);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusSdioEnumerateFunctions: existing function PDO set no "
                "longer matches the card (0x%08lx)\n", Status);
        return Status;
    }
    if (Reused)
    {
        DPRINT1("SdBusSdioEnumerateFunctions: reused %lu function PDOs for "
                "insertion generation %lu\n", NumFunctions,
                HostPdo->InsertionGeneration);
        return STATUS_SUCCESS;
    }

    for (FunctionIndex = 0; FunctionIndex < NumFunctions; FunctionIndex++)
    {
        PSDBUS_SDIO_FUNCTION_INFO Info = &FunctionInfo[FunctionIndex];
        Status = SdBusCreateSdioFunctionPdo(FdoExtension, HostPdo,
                                             (UCHAR)(FunctionIndex + 1),
                                             Info->VendorId,
                                             Info->DeviceId,
                                             Info->FunctionClass,
                                             &FunctionPdos[FunctionIndex]);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusSdioEnumerateFunctions: func %lu PDO creation failed "
                    "(0x%08lx)\n", FunctionIndex + 1, Status);
            while (CreatedCount != 0)
            {
                CreatedCount--;
                IoDeleteDevice(FunctionPdos[CreatedCount]);
            }
            return Status;
        }
        CreatedCount++;
    }

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    for (FunctionIndex = 0; FunctionIndex < NumFunctions; FunctionIndex++)
    {
        PPDO_EXTENSION FunctionPdoExtension;

        FunctionPdoExtension = (PPDO_EXTENSION)
            FunctionPdos[FunctionIndex]->DeviceExtension;
        FunctionPdos[FunctionIndex]->Flags &= ~DO_DEVICE_INITIALIZING;
        InsertTailList(&FdoExtension->ChildPdoList,
                       &FunctionPdoExtension->ListEntry);
        FdoExtension->ChildPdoCount++;
    }
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    for (FunctionIndex = 0; FunctionIndex < NumFunctions; FunctionIndex++)
    {
        PSDBUS_SDIO_FUNCTION_INFO Info = &FunctionInfo[FunctionIndex];

        DPRINT1("SdBusCreateSdioFunctionPdo: func=%lu VID=%04X DID=%04X "
                "class=%u PDO=%p\n", FunctionIndex + 1,
                Info->VendorId, Info->DeviceId, Info->FunctionClass,
                FunctionPdos[FunctionIndex]);
    }

    return STATUS_SUCCESS;
}
