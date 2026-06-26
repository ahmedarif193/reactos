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

    if (FdoExtension == NULL)
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

    if (DataOut != NULL)
    {
        *DataOut = (UCHAR)(Response & 0xFF);
    }

    return STATUS_SUCCESS;
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

    UNREFERENCED_PARAMETER(PdoExtension);

    if (FdoExtension == NULL)
    {
        return STATUS_INVALID_PARAMETER;
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
        TransferBlockSize = ActualCount;
        DataLength = ActualCount;
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

    return SdBusSendSdhciCommand(FdoExtension, &CmdDesc, Argument,
                                 Mdl, DataLength, TransferBlockSize, &Response);
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

    while (BytesWalked < SDIO_CIS_WALK_MAX_BYTES)
    {
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

        Status = SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                                Address + 1, 0, &TupleLink);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        BodyStart = Address + 2;

        switch (TupleCode)
        {
            case CISTPL_MANFID:
            {
                UCHAR VidLo = 0, VidHi = 0, DidLo = 0, DidHi = 0;

                if (TupleLink < 4)
                {
                    break;
                }
                SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                               BodyStart + 0, 0, &VidLo);
                SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                               BodyStart + 1, 0, &VidHi);
                SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                               BodyStart + 2, 0, &DidLo);
                SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                               BodyStart + 3, 0, &DidHi);

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
                SdBusSdioCmd52(FdoExtension, 0, FALSE, FALSE,
                               BodyStart + 0, 0, &Class);
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
    _In_ UCHAR Class)
{
    PDEVICE_OBJECT Pdo;
    PPDO_EXTENSION PdoExtension;
    NTSTATUS Status;
    KIRQL OldIrql;

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
    PdoExtension->InterfaceReferenceCount = 0;

    PdoExtension->CardType = HostPdo->CardType;
    PdoExtension->RelativeAddress = HostPdo->RelativeAddress;
    PdoExtension->FunctionNumber = FunctionNumber;
    PdoExtension->SdioVendorId = Vid;
    PdoExtension->SdioDeviceId = Did;
    PdoExtension->SdioClass = Class;

    PdoExtension->InsertionGeneration = HostPdo->InsertionGeneration;

    InitializeListHead(&PdoExtension->ListEntry);
    IoInitializeRemoveLock(&PdoExtension->RemoveLock, TAG_SDBUS, 0, 0);

    Pdo->Flags |= DO_DIRECT_IO;
    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
    InsertTailList(&FdoExtension->ChildPdoList, &PdoExtension->ListEntry);
    FdoExtension->ChildPdoCount++;
    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    DPRINT1("SdBusCreateSdioFunctionPdo: func=%u VID=%04X DID=%04X class=%u PDO=%p\n",
            FunctionNumber, Vid, Did, Class, Pdo);

    return STATUS_SUCCESS;
}

NTSTATUS
SdBusSdioEnumerateFunctions(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PPDO_EXTENSION HostPdo,
    _In_ ULONG NumFunctions)
{
    NTSTATUS Status;
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

    if (FdoExtension == NULL || HostPdo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (NumFunctions == 0)
    {
        DPRINT1("SdBusSdioEnumerateFunctions: No SDIO functions to enumerate\n");
        return STATUS_SUCCESS;
    }

    if (NumFunctions > 7)
    {
        NumFunctions = 7;
    }

    (void)SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_REVISION, &CccrRev);
    (void)SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_SD_SPEC, &SdSpecRev);
    (void)SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_CARD_CAPABILITY, &CardCap);
    (void)SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_BUS_INTERFACE, &BusIfCtrl);
    (void)SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_UHS_SUPPORT, &UhsSupport);

    Status = SdBusSdioReadCisPointer(FdoExtension, 0, SDIO_CCCR_CIS_POINTER,
                                     &CommonCisPointer);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusSdioEnumerateFunctions: common CIS pointer read failed "
                "(0x%08lx)\n", Status);
        CommonCisPointer = 0;
    }

    DPRINT1("SdBusSdioEnumerateFunctions: CCCR rev=0x%02X spec=0x%02X cap=0x%02X "
            "bus_if=0x%02X uhs=0x%02X CIS=0x%06lX NumFuncs=%lu\n",
            CccrRev, SdSpecRev, CardCap, BusIfCtrl, UhsSupport,
            CommonCisPointer, NumFunctions);

    if (CommonCisPointer != 0)
    {
        (void)SdBusSdioWalkCis(FdoExtension, CommonCisPointer,
                               &CommonVid, &CommonDid, &CommonClass);
        DPRINT1("SdBusSdioEnumerateFunctions: Common CIS VID=0x%04X DID=0x%04X "
                "class=0x%02X\n", CommonVid, CommonDid, CommonClass);
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

        if (NT_SUCCESS(SdBusSdioWriteCccr(FdoExtension, SDIO_CCCR_BUS_INTERFACE, BusIf)))
        {
            HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
            HostCtrl |= SDHCI_HC_DATA_WIDTH_4BIT;
            SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
            FdoExtension->CurrentBusWidth = 4;
            DPRINT1("SdBusSdioEnumerateFunctions: SDIO 4-bit bus enabled\n");
        }

        (void)SdBusSdioReadCccr(FdoExtension, SDIO_CCCR_HIGH_SPEED, &HighSpeed);
        if (HighSpeed & 0x01)
        {
            HighSpeed |= 0x02;
            if (NT_SUCCESS(SdBusSdioWriteCccr(FdoExtension, SDIO_CCCR_HIGH_SPEED, HighSpeed)))
            {
                HostCtrl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL);
                HostCtrl |= SDHCI_HC_HIGH_SPEED;
                SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostCtrl);
                DPRINT1("SdBusSdioEnumerateFunctions: SDIO high-speed enabled\n");
            }
        }
    }

    for (FunctionIndex = 1; FunctionIndex <= NumFunctions; FunctionIndex++)
    {
        ULONG FbrBase = SDIO_FBR_BASE(FunctionIndex);
        UCHAR StdInterface = 0;
        UCHAR ExtInterface = 0;
        ULONG FuncCisPointer = 0;
        USHORT FuncVid = CommonVid;
        USHORT FuncDid = CommonDid;
        UCHAR FuncClass = 0;

        (void)SdBusSdioReadCccr(FdoExtension,
                                FbrBase + SDIO_FBR_STD_INTERFACE,
                                &StdInterface);
        (void)SdBusSdioReadCccr(FdoExtension,
                                FbrBase + SDIO_FBR_EXT_INTERFACE,
                                &ExtInterface);

        FuncClass = (StdInterface & 0x0F);
        if (FuncClass == 0x0F)
        {
            FuncClass = ExtInterface;
        }

        Status = SdBusSdioReadCisPointer(FdoExtension, (UCHAR)FunctionIndex,
                                         FbrBase + SDIO_FBR_CIS_POINTER,
                                         &FuncCisPointer);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusSdioEnumerateFunctions: func %lu CIS pointer read "
                    "failed (0x%08lx)\n", FunctionIndex, Status);
            FuncCisPointer = 0;
        }

        if (FuncCisPointer != 0)
        {
            USHORT TupleVid = 0;
            USHORT TupleDid = 0;
            UCHAR TupleClass = 0;

            (void)SdBusSdioWalkCis(FdoExtension, FuncCisPointer,
                                   &TupleVid, &TupleDid, &TupleClass);

            if (TupleVid != 0)
            {
                FuncVid = TupleVid;
            }
            if (TupleDid != 0)
            {
                FuncDid = TupleDid;
            }
            if (TupleClass != 0 && FuncClass == 0)
            {
                FuncClass = TupleClass;
            }
        }

        DPRINT1("SdBusSdioEnumerateFunctions: func %lu std=0x%02X ext=0x%02X "
                "class=0x%02X CIS=0x%06lX VID=0x%04X DID=0x%04X\n",
                FunctionIndex, StdInterface, ExtInterface, FuncClass,
                FuncCisPointer, FuncVid, FuncDid);

        Status = SdBusCreateSdioFunctionPdo(FdoExtension, HostPdo,
                                             (UCHAR)FunctionIndex,
                                             FuncVid, FuncDid, FuncClass);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusSdioEnumerateFunctions: func %lu PDO creation failed "
                    "(0x%08lx), continuing with siblings\n",
                    FunctionIndex, Status);
        }
    }

    return STATUS_SUCCESS;
}
