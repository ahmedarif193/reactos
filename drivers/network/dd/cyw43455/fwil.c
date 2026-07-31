/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCDC/SDPCM control transport, fil commands, escan and events
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"

#define NDEBUG
#include <debug.h>

#define SDPCM_HEADER_LEN        12
#define SDPCM_CHANNEL_CONTROL   0
#define SDPCM_CHANNEL_EVENT     1
#define SDPCM_CHANNEL_DATA      2
#define SDPCM_CHANNEL_GLOM      3
#define SDPCM_GLOMDESC_FLAG     0x80
#define SDPCM_SEQ_OFFSET        4
#define SDPCM_CHANNEL_OFFSET    5
#define SDPCM_NEXTLEN_OFFSET    6
#define SDPCM_DOFFSET_OFFSET    7
#define CYW_FIRSTREAD           64
#define CYW_RXBOUND             50
#define CYW_RX_POLL_FALLBACK    (-100000LL) /* 10 ms; interrupts are primary */

#define SDIO_F2_FIFO            0x8000

static
VOID
CywWriteSdpcmHeader(
    _In_ PCYW_ADAPTER Adapter,
    _Out_ PUCHAR Frame,
    _In_ ULONG Total,
    _In_ ULONG TailPad,
    _In_ UCHAR Channel,
    _In_ ULONG HdrLen)
{
    PUCHAR Sw = Frame + HdrLen - 8;

    RtlZeroMemory(Frame, HdrLen);
    Frame[0] = (UCHAR)(Total & 0xFF);
    Frame[1] = (UCHAR)((Total >> 8) & 0xFF);
    Frame[2] = (UCHAR)(~Frame[0]);
    Frame[3] = (UCHAR)(~Frame[1]);
    if (HdrLen > SDPCM_HEADER_LEN)
    {
        ULONG Tag = (Total - 4) | (1ul << 24);
        Frame[4] = (UCHAR)(Tag & 0xFF);
        Frame[5] = (UCHAR)((Tag >> 8) & 0xFF);
        Frame[6] = (UCHAR)((Tag >> 16) & 0xFF);
        Frame[7] = (UCHAR)((Tag >> 24) & 0xFF);
        Frame[10] = (UCHAR)(TailPad & 0xFF);
        Frame[11] = (UCHAR)((TailPad >> 8) & 0xFF);
    }
    Sw[1] = Channel;
    Sw[3] = (UCHAR)HdrLen;
}

/* Move Length bytes to/from the F2 FIFO as whole blocks plus a ULONG-aligned
 * byte remainder. Length is expected block-or-ULONG sized by the caller. */
static
NTSTATUS
CywSdpcmF2Fifo(
    _In_ PCYW_ADAPTER Adapter,
    _In_ BOOLEAN Write,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Blocks = Length / CYW_F2_BLOCKSIZE;
    ULONG Done = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Adapter == NULL || Buffer == NULL || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Blocks > 0)
    {
        ULONG BlockBytes = Blocks * CYW_F2_BLOCKSIZE;
        Status = Write
            ? CywSdioWriteBlocks(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                 Buffer, BlockBytes, CYW_F2_BLOCKSIZE)
            : CywSdioReadBlocks(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                Buffer, BlockBytes, CYW_F2_BLOCKSIZE);
        Done = BlockBytes;
    }
    if (NT_SUCCESS(Status) && Done < Length)
    {
        ULONG Rest = ALIGN_UP(Length - Done, ULONG);
        Status = Write
            ? CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                Buffer + Done, Rest)
            : CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                               Buffer + Done, Rest);
    }
    return Status;
}

static
NTSTATUS
CywSdpcmSendCtl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Data,
    _In_ ULONG Length)
{
    PUCHAR Frame;
    ULONG Total;
    ULONG Padded;
    NTSTATUS Status;

    if (Adapter == NULL || Data == NULL || Length == 0 ||
        Length > CYW_CONTROL_BUFFER_SIZE - SDPCM_HEADER_LEN)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Frame = Adapter->ControlBuffer;
    Total = SDPCM_HEADER_LEN + Length;
    Padded = ALIGN_UP(Total, ULONG);
    if (Padded > CYW_CONTROL_BUFFER_SIZE)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    CywWriteSdpcmHeader(Adapter, Frame, Total, Padded - Total, SDPCM_CHANNEL_CONTROL, SDPCM_HEADER_LEN);

    RtlCopyMemory(Frame + SDPCM_HEADER_LEN, Data, Length);

    KeWaitForSingleObject(&Adapter->F2Lock, Executive, KernelMode, FALSE, NULL);
    Frame[SDPCM_HEADER_LEN - 8] = Adapter->TxSeq;
    Status = CywSdpcmF2Fifo(Adapter, TRUE, Frame, Padded);
    if (NT_SUCCESS(Status))
    {
        Adapter->TxSeq++;
    }
    KeReleaseMutex(&Adapter->F2Lock, FALSE);
    return Status;
}

static
VOID
CywUpdateCredits(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Frame)
{
    UCHAR Max = Frame[9];

    if ((UCHAR)(Max - Adapter->TxSeq) <= 0x40)
    {
        Adapter->TxMax = Max;
    }
    Adapter->TxFlow = Frame[8];
}

NTSTATUS
CywSdpcmSendData(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Eth,
    _In_ ULONG EthLen)
{
    PUCHAR Frame;
    ULONG Total;
    ULONG Padded;
    UCHAR Avail;
    NTSTATUS Status;

    if (Adapter == NULL || Eth == NULL || EthLen == 0 ||
        EthLen > CYW_CONTROL_BUFFER_SIZE - SDPCM_HEADER_LEN - BCDC_HEADER_LEN)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Frame = Adapter->TxBuffer;
    Total = SDPCM_HEADER_LEN + BCDC_HEADER_LEN + EthLen;
    Padded = ALIGN_UP(Total, ULONG);

    Avail = (UCHAR)(Adapter->TxMax - Adapter->TxSeq);
    if ((Avail & 0x80) || Avail == 0 || Adapter->TxFlow)
    {
        return STATUS_DEVICE_BUSY;
    }

    if (Padded > CYW_CONTROL_BUFFER_SIZE)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    KeWaitForSingleObject(&Adapter->F2Lock, Executive, KernelMode, FALSE, NULL);

    Avail = (UCHAR)(Adapter->TxMax - Adapter->TxSeq);
    if ((Avail & 0x80) || Avail == 0 || Adapter->TxFlow)
    {
        KeReleaseMutex(&Adapter->F2Lock, FALSE);
        return STATUS_DEVICE_BUSY;
    }

    CywWriteSdpcmHeader(Adapter, Frame, Total, Padded - Total, SDPCM_CHANNEL_DATA, SDPCM_HEADER_LEN);
    RtlZeroMemory(Frame + SDPCM_HEADER_LEN, BCDC_HEADER_LEN);
    Frame[SDPCM_HEADER_LEN] = (UCHAR)(BCDC_PROTO_VER << 4);

    RtlCopyMemory(Frame + SDPCM_HEADER_LEN + BCDC_HEADER_LEN, Eth, EthLen);

    Frame[SDPCM_HEADER_LEN - 8] = Adapter->TxSeq;
    Status = CywSdpcmF2Fifo(Adapter, TRUE, Frame, Padded);
    if (NT_SUCCESS(Status))
    {
        Adapter->TxSeq = (UCHAR)(Adapter->TxSeq + 1);
        InterlockedIncrement64(&Adapter->TxOkCount);
    }
    else
    {
        InterlockedIncrement64(&Adapter->TxErrCount);
    }
    KeReleaseMutex(&Adapter->F2Lock, FALSE);
    return Status;
}

/* Immediate transmit: reframe the NET_BUFFER straight into the TX staging
 * buffer under F2Lock, so the send path needs no intermediate frame copy. */
NTSTATUS
CywSdpcmSendNb(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNET_BUFFER Nb)
{
    PUCHAR Frame;
    PUCHAR Dest;
    ULONG Capacity = CYW_CONTROL_BUFFER_SIZE - SDPCM_HEADER_LEN - BCDC_HEADER_LEN;
    ULONG EthLen;
    ULONG TxPadded;
    ULONG Total;
    UCHAR Avail;
    NTSTATUS Status;

    if (Adapter == NULL || Nb == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Frame = Adapter->TxBuffer;
    Dest = Frame + SDPCM_HEADER_LEN + BCDC_HEADER_LEN;

    Avail = (UCHAR)(Adapter->TxMax - Adapter->TxSeq);
    if ((Avail & 0x80) || Avail == 0 || Adapter->TxFlow)
    {
        return STATUS_DEVICE_BUSY;
    }

    KeWaitForSingleObject(&Adapter->F2Lock, Executive, KernelMode, FALSE, NULL);

    Avail = (UCHAR)(Adapter->TxMax - Adapter->TxSeq);
    if ((Avail & 0x80) || Avail == 0 || Adapter->TxFlow)
    {
        KeReleaseMutex(&Adapter->F2Lock, FALSE);
        return STATUS_DEVICE_BUSY;
    }

    EthLen = CywBuildEthFromNbl(Nb, Dest, Capacity);
    if (EthLen == 0 || !CywTxAllowed(Adapter, Dest, EthLen))
    {
        KeReleaseMutex(&Adapter->F2Lock, FALSE);
        return (EthLen == 0) ? STATUS_INVALID_PARAMETER : STATUS_INVALID_DEVICE_STATE;
    }

    Total = SDPCM_HEADER_LEN + BCDC_HEADER_LEN + EthLen;
    TxPadded = ALIGN_UP(Total, ULONG);
    CywWriteSdpcmHeader(Adapter, Frame, Total, TxPadded - Total, SDPCM_CHANNEL_DATA, SDPCM_HEADER_LEN);
    RtlZeroMemory(Frame + SDPCM_HEADER_LEN, BCDC_HEADER_LEN);
    Frame[SDPCM_HEADER_LEN] = (UCHAR)(BCDC_PROTO_VER << 4);
    Frame[SDPCM_HEADER_LEN - 8] = Adapter->TxSeq;

    Status = CywSdpcmF2Fifo(Adapter, TRUE, Frame, TxPadded);
    if (NT_SUCCESS(Status))
    {
        Adapter->TxSeq = (UCHAR)(Adapter->TxSeq + 1);
        InterlockedIncrement64(&Adapter->TxOkCount);
    }
    else
    {
        InterlockedIncrement64(&Adapter->TxErrCount);
    }
    KeReleaseMutex(&Adapter->F2Lock, FALSE);
    return Status;
}

static
NTSTATUS
CywSdpcmRecvCtl(
    _In_ PCYW_ADAPTER Adapter,
    _Out_ PUCHAR Data,
    _Inout_ PULONG Length)
{
    PUCHAR Frame;
    NTSTATUS Status;
    ULONG Retry;
    ULONG FrameLen;
    ULONG DataOffset;
    ULONG Payload;
    ULONG Capacity;
    LARGE_INTEGER Delay;

    if (Adapter == NULL || Data == NULL || Length == NULL || *Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Frame = Adapter->ControlBuffer;
    Capacity = *Length;

    for (Retry = 0; Retry < 1000; Retry++)
    {
        Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                  Frame, SDPCM_HEADER_LEN);
        if (NT_SUCCESS(Status))
        {
            FrameLen = Frame[0] | (Frame[1] << 8);
            if (FrameLen >= SDPCM_HEADER_LEN &&
                FrameLen <= CYW_CONTROL_BUFFER_SIZE &&
                ((Frame[0] ^ Frame[2]) == 0xFF) &&
                ((Frame[1] ^ Frame[3]) == 0xFF))
            {
                CywUpdateCredits(Adapter, Frame);
                DataOffset = Frame[SDPCM_DOFFSET_OFFSET];
                if (DataOffset >= SDPCM_HEADER_LEN && DataOffset < FrameLen)
                {
                    UCHAR Channel = Frame[SDPCM_CHANNEL_OFFSET] & 0x0F;
                    if (FrameLen > SDPCM_HEADER_LEN)
                    {
                        Status = CywSdpcmF2Fifo(Adapter, FALSE,
                                                Frame + SDPCM_HEADER_LEN,
                                                ALIGN_UP(FrameLen - SDPCM_HEADER_LEN, ULONG));
                        if (!NT_SUCCESS(Status))
                        {
                            Delay.QuadPart = -10000LL;
                            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
                            continue;
                        }
                    }
                    if (Channel == SDPCM_CHANNEL_EVENT)
                    {
                        CywProcessEvent(Adapter, Frame + DataOffset, FrameLen - DataOffset);
                    }
                    else if (Channel == SDPCM_CHANNEL_CONTROL)
                    {
                        Payload = FrameLen - DataOffset;
                        if (Payload > Capacity)
                        {
                            *Length = Payload;
                            return STATUS_BUFFER_TOO_SMALL;
                        }
                        RtlCopyMemory(Data, Frame + DataOffset, Payload);
                        *Length = Payload;
                        return STATUS_SUCCESS;
                    }
                }
            }
        }
        Delay.QuadPart = -10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
CywBcdcXfer(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Cmd,
    _In_ BOOLEAN Set,
    _Inout_ PVOID Data,
    _In_ ULONG Length)
{
    PUCHAR Msg;
    PCYW_BCDC_DCMD Dcmd;
    ULONG MsgLen;
    ULONG BufLen;
    ULONG RecvLen = 0;
    ULONG ReqId;
    NTSTATUS Status;

    if (Adapter == NULL || (Length != 0 && Data == NULL) ||
        Length > CYW_CONTROL_BUFFER_SIZE - SDPCM_HEADER_LEN -
                     sizeof(CYW_BCDC_DCMD))
    {
        return STATUS_INVALID_PARAMETER;
    }

    MsgLen = sizeof(CYW_BCDC_DCMD) + Length;
    if (MsgLen > MAXULONG - sizeof(CYW_BCDC_DCMD))
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    BufLen = MsgLen + sizeof(CYW_BCDC_DCMD);

    Msg = CywAllocate(BufLen);
    if (Msg == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeWaitForSingleObject(&Adapter->CmdLock, Executive, KernelMode, FALSE, NULL);

    Adapter->BcdcRequestId++;
    ReqId = Adapter->BcdcRequestId &
            (BCDC_DCMD_ID_MASK >> BCDC_DCMD_ID_SHIFT);

    Dcmd = (PCYW_BCDC_DCMD)Msg;
    Dcmd->Cmd = Cmd;
    Dcmd->Len = Length;
    Dcmd->Flags = (ReqId << BCDC_DCMD_ID_SHIFT) | BCDC_PROTO_VER;
    if (Set)
    {
        Dcmd->Flags |= BCDC_DCMD_SET;
    }
    Dcmd->Status = 0;

    if (Length > 0 && Data != NULL)
    {
        RtlCopyMemory(Msg + sizeof(CYW_BCDC_DCMD), Data, Length);
    }

    if (InterlockedCompareExchange(&Adapter->BusThreadRunning, 0, 0) != 0)
    {
        LARGE_INTEGER Timeout;

        KeClearEvent(&Adapter->CtrlEvent);
        Adapter->CtrlResponseLen = 0;
        InterlockedExchange(&Adapter->CtrlResponseStatus, STATUS_PENDING);
        InterlockedExchange(&Adapter->ExpectedBcdcCommand, (LONG)Cmd);
        InterlockedExchange(&Adapter->ExpectedBcdcRequestId, (LONG)ReqId);
        Status = CywSdpcmSendCtl(Adapter, Msg, MsgLen);
        if (NT_SUCCESS(Status))
        {
            Timeout.QuadPart = -20000000;
            Status = KeWaitForSingleObject(&Adapter->CtrlEvent, Executive,
                                           KernelMode, FALSE, &Timeout);
            if (Status == STATUS_SUCCESS)
            {
                Status = (NTSTATUS)InterlockedCompareExchange(
                    &Adapter->CtrlResponseStatus, 0, 0);
                if (NT_SUCCESS(Status))
                {
                    RecvLen = Adapter->CtrlResponseLen;
                    if (RecvLen > BufLen)
                    {
                        Status = STATUS_BUFFER_OVERFLOW;
                    }
                    else
                    {
                        RtlCopyMemory(Msg, Adapter->ControlBuffer, RecvLen);
                    }
                }
            }
            else
            {
                DPRINT1("CYW: BCDC cmd %lu control-response wait failed 0x%08lx (timeout)\n", (ULONG)Cmd, Status);
                Status = STATUS_IO_TIMEOUT;
            }
        }
        InterlockedExchange(&Adapter->ExpectedBcdcRequestId, -1);
        InterlockedExchange(&Adapter->ExpectedBcdcCommand, -1);
    }
    else
    {
        Status = CywSdpcmSendCtl(Adapter, Msg, MsgLen);
        if (NT_SUCCESS(Status))
        {
            RecvLen = BufLen;
            Status = CywSdpcmRecvCtl(Adapter, Msg, &RecvLen);
        }
    }

    if (NT_SUCCESS(Status) && RecvLen < sizeof(CYW_BCDC_DCMD))
    {
        Status = STATUS_DEVICE_DATA_ERROR;
    }

    if (NT_SUCCESS(Status))
    {
        ULONG ResponseId;
        ULONG ResponsePayload;

        Dcmd = (PCYW_BCDC_DCMD)Msg;
        ResponseId = (Dcmd->Flags & BCDC_DCMD_ID_MASK) >>
                     BCDC_DCMD_ID_SHIFT;
        ResponsePayload = RecvLen - sizeof(CYW_BCDC_DCMD);
        if (Dcmd->Cmd != Cmd || ResponseId != ReqId)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
        }
        else if (Dcmd->Flags & BCDC_DCMD_ERROR)
        {
            DPRINT1("CYW: BCDC cmd %lu firmware error %ld\n", (ULONG)Cmd, (LONG)Dcmd->Status);
            Status = STATUS_UNSUCCESSFUL;
        }
        else if (!Set && Data != NULL)
        {
            ULONG Copy;

            if (Dcmd->Len > ResponsePayload)
            {
                Status = STATUS_DEVICE_DATA_ERROR;
                Copy = 0;
            }
            else
            {
                Copy = Dcmd->Len;
            }
            if (Copy > Length)
            {
                Copy = Length;
            }
            if (NT_SUCCESS(Status))
            {
                RtlZeroMemory(Data, Length);
                RtlCopyMemory(Data, Msg + sizeof(CYW_BCDC_DCMD), Copy);
            }
        }
    }

    KeReleaseMutex(&Adapter->CmdLock, FALSE);
    CywFree(Msg);
    return Status;
}

NTSTATUS
CywFilCmdSet(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Cmd,
    _In_ PVOID Data,
    _In_ ULONG Length)
{
    return CywBcdcXfer(Adapter, Cmd, TRUE, Data, Length);
}

NTSTATUS
CywFilCmdGet(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Cmd,
    _Out_ PVOID Data,
    _In_ ULONG Length)
{
    return CywBcdcXfer(Adapter, Cmd, FALSE, Data, Length);
}

static
NTSTATUS
CywFilIovar(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCSTR Name,
    _In_ BOOLEAN Set,
    _Inout_ PVOID Data,
    _In_ ULONG Length)
{
    PUCHAR Buffer;
    ULONG NameLen = 0;
    ULONG Total;
    ULONG MaxPayload;
    NTSTATUS Status;

    if (Adapter == NULL || Name == NULL ||
        (Length != 0 && Data == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    MaxPayload = CYW_CONTROL_BUFFER_SIZE - SDPCM_HEADER_LEN -
                 sizeof(CYW_BCDC_DCMD);
    while (NameLen < MaxPayload && Name[NameLen] != ANSI_NULL)
    {
        NameLen++;
    }
    if (NameLen == 0 || NameLen == MaxPayload)
    {
        return STATUS_INVALID_PARAMETER;
    }
    NameLen++;
    if (Length > MaxPayload - NameLen)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    Total = NameLen + Length;

    Buffer = CywAllocate(Total);
    if (Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Buffer, Name, NameLen);
    if (Set && Length > 0 && Data != NULL)
    {
        RtlCopyMemory(Buffer + NameLen, Data, Length);
    }

    Status = CywBcdcXfer(Adapter, Set ? BRCMF_C_SET_VAR : BRCMF_C_GET_VAR,
                         Set, Buffer, Total);

    if (!Set && NT_SUCCESS(Status) && Length > 0 && Data != NULL)
    {
        RtlCopyMemory(Data, Buffer, Length);
    }

    CywFree(Buffer);
    return Status;
}

NTSTATUS
CywFilIovarSet(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCSTR Name,
    _In_ PVOID Data,
    _In_ ULONG Length)
{
    return CywFilIovar(Adapter, Name, TRUE, Data, Length);
}

NTSTATUS
CywFilIovarGet(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCSTR Name,
    _Out_ PVOID Data,
    _In_ ULONG Length)
{
    return CywFilIovar(Adapter, Name, FALSE, Data, Length);
}

NTSTATUS
CywFilIovarSetInt(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCSTR Name,
    _In_ ULONG Value)
{
    ULONG Local = Value;
    return CywFilIovar(Adapter, Name, TRUE, &Local, sizeof(Local));
}

NTSTATUS
CywActivateEvents(
    _In_ PCYW_ADAPTER Adapter)
{
    UCHAR Mask[16];
    static const UCHAR Events[] =
    {
        BRCMF_E_SET_SSID, BRCMF_E_JOIN, BRCMF_E_AUTH, BRCMF_E_DEAUTH,
        BRCMF_E_DEAUTH_IND, BRCMF_E_ASSOC, BRCMF_E_REASSOC,
        BRCMF_E_REASSOC_IND, BRCMF_E_DISASSOC, BRCMF_E_DISASSOC_IND,
        BRCMF_E_LINK, BRCMF_E_MIC_ERROR, BRCMF_E_ROAM, BRCMF_E_IF,
        BRCMF_E_ESCAN_RESULT
    };
    ULONG i;
    NTSTATUS Status;

    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywFilIovarGet(Adapter, "event_msgs", Mask, sizeof(Mask));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    for (i = 0; i < sizeof(Events); i++)
    {
        Mask[Events[i] >> 3] |= (UCHAR)(1u << (Events[i] & 7));
    }

    return CywFilIovarSet(Adapter, "event_msgs", Mask, sizeof(Mask));
}

NTSTATUS
CywQueryRssi(
    _In_ PCYW_ADAPTER Adapter,
    _Out_ PLONG Rssi)
{
    CYW_SCB_VAL_LE Scb;
    NTSTATUS Status;

    if (Adapter == NULL || Rssi == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Scb, sizeof(Scb));
    Status = CywFilCmdGet(Adapter, BRCMF_C_GET_RSSI, &Scb, sizeof(Scb));
    if (NT_SUCCESS(Status))
    {
        *Rssi = (LONG)Scb.Val;
    }
    return Status;
}

/* Current TX rate in 500 kbit/s units */
NTSTATUS
CywQueryRate(
    _In_ PCYW_ADAPTER Adapter,
    _Out_ PULONG RateUnits500Kbps)
{
    ULONG Value = 0;
    NTSTATUS Status;

    if (Adapter == NULL || RateUnits500Kbps == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywFilCmdGet(Adapter, BRCMF_C_GET_RATE, &Value, sizeof(Value));
    if (NT_SUCCESS(Status))
    {
        *RateUnits500Kbps = Value;
    }
    return Status;
}

NTSTATUS
CywScanStart(
    _In_ PCYW_ADAPTER Adapter,
    _In_reads_bytes_opt_(RequestLength) PDOT11_SCAN_REQUEST_V2 Request,
    _In_ ULONG RequestLength)
{
    PCYW_ESCAN_PARAMS_LE Params;
    ULONG Size = CYW_ESCAN_PARAMS_FIXED_SIZE;
    ULONG FixedSize = FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer);
    PDOT11_SSID Ssids = NULL;
    ULONG SsidCount;
    ULONG i;
    NTSTATUS Status;

    if (Request == NULL || RequestLength < FixedSize)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if ((Request->dot11ScanType & ~dot11_scan_type_forced) < dot11_scan_type_active ||
        (Request->dot11ScanType & ~dot11_scan_type_forced) > dot11_scan_type_auto)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Request->dot11BSSType < dot11_BSS_type_infrastructure ||
        Request->dot11BSSType > dot11_BSS_type_any)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SsidCount = Request->uNumOfdot11SSIDs;
    if (SsidCount > CYW_SCAN_MAX_SSIDS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (SsidCount != 0)
    {
        ULONG Offset = Request->udot11SSIDsOffset;

        if (Offset < FixedSize || Offset > RequestLength ||
            SsidCount > (RequestLength - Offset) / sizeof(DOT11_SSID))
        {
            return STATUS_INVALID_PARAMETER;
        }
        Ssids = (PDOT11_SSID)((PUCHAR)Request + Offset);
        for (i = 0; i < SsidCount; i++)
        {
            if (Ssids[i].uSSIDLength > DOT11_SSID_MAX_LENGTH)
            {
                return STATUS_INVALID_PARAMETER;
            }
        }
    }
    if (Request->uIEsLength != 0 &&
        (Request->uIEsOffset < FixedSize ||
         Request->uIEsOffset > RequestLength ||
         Request->uIEsLength > RequestLength - Request->uIEsOffset))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (SsidCount > 1)
    {
        Size += SsidCount * sizeof(CYW_SSID_LE);
    }

    Params = CywAllocate(Size);
    if (Params == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Params->Version = BRCMF_ESCAN_REQ_VERSION;
    Params->Action = WL_ESCAN_ACTION_START;
    Params->SyncId = CYW_ESCAN_SYNCID;

    Params->Params.Ssid.SsidLen = 0;
    RtlCopyMemory(Params->Params.Bssid, Request->dot11BSSID,
                  CYW_ADDRESS_LENGTH);
    Params->Params.BssType = (CHAR)(Request->dot11BSSType - 1);
    Params->Params.ScanType = BRCMF_SCANTYPE_ACTIVE;
    Params->Params.NProbes = -1;
    Params->Params.ActiveTime = -1;
    Params->Params.PassiveTime = -1;
    Params->Params.HomeTime = -1;
    Params->Params.ChannelNum = 0;

    /* Honor the OS scan request: passive scans and directed (SSID) scans,
     * needed to discover hidden networks */
    if ((Request->dot11ScanType & ~dot11_scan_type_forced) ==
        dot11_scan_type_passive)
    {
        Params->Params.ScanType = BRCMF_SCANTYPE_PASSIVE;
    }
    if (SsidCount == 1 && Ssids[0].uSSIDLength != 0)
    {
        Params->Params.Ssid.SsidLen = Ssids[0].uSSIDLength;
        RtlCopyMemory(Params->Params.Ssid.Ssid, Ssids[0].ucSSID,
                      Ssids[0].uSSIDLength);
    }
    else if (SsidCount > 1)
    {
        PCYW_SSID_LE FirmwareSsids =
            (PCYW_SSID_LE)((PUCHAR)Params + CYW_ESCAN_PARAMS_FIXED_SIZE);

        for (i = 0; i < SsidCount; i++)
        {
            FirmwareSsids[i].SsidLen = Ssids[i].uSSIDLength;
            RtlCopyMemory(FirmwareSsids[i].Ssid, Ssids[i].ucSSID,
                          Ssids[i].uSSIDLength);
        }
        Params->Params.ChannelNum = (LONG)(SsidCount << BRCMF_SCAN_PARAMS_NSSID_SHIFT);
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->BssCount = 0;
    Adapter->ScanInProgress = TRUE;
    NdisReleaseSpinLock(&Adapter->Lock);

    Status = CywFilIovarSet(Adapter, "escan", Params, Size);

    CywFree(Params);

    if (!NT_SUCCESS(Status))
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->ScanInProgress = FALSE;
        NdisReleaseSpinLock(&Adapter->Lock);
    }

    return Status;
}

static
USHORT
CywSelectJoinChanspecLocked(
    _In_ PCYW_ADAPTER Adapter,
    _In_reads_(SsidLength) const UCHAR *Ssid,
    _In_ ULONG SsidLength,
    _In_reads_(CYW_ADDRESS_LENGTH) const UCHAR *Bssid,
    _In_ BOOLEAN HasBssid)
{
    ULONG i;
    USHORT Best = 0;
    LONG BestRssi = 0;
    BOOLEAN BestIs5G = FALSE;

    if (SsidLength == 0)
    {
        return 0;
    }

    for (i = 0; i < Adapter->BssCount; i++)
    {
        PCYW_BSS Bss = &Adapter->Bss[i];
        ULONG Chan = Bss->ChannelNumber;
        USHORT Chspec;
        BOOLEAN Is5G;
        BOOLEAN Better;

        if (Bss->SsidLength != SsidLength ||
            !RtlEqualMemory(Bss->Ssid, Ssid, SsidLength))
        {
            continue;
        }

        if (Chan == 0 || Chan > CYW_CHAN_MAX_5G || (Chan > CYW_CHAN_MAX_2G && Chan < 36))
        {
            continue;
        }

        Chspec = (USHORT)(Chan | CYW_CHSPEC_BW_20 | (Chan > CYW_CHAN_MAX_2G ? CYW_CHSPEC_BND_5G : CYW_CHSPEC_BND_2G));

        if (HasBssid &&
            !RtlEqualMemory(Bss->Bssid, Bssid, CYW_ADDRESS_LENGTH))
        {
            continue;
        }

        Is5G = (Chan > CYW_CHAN_MAX_2G) && (Bss->Rssi == 0 || Bss->Rssi >= CYW_5G_PREFER_RSSI);

        if (Best == 0)
        {
            Better = TRUE;
        }
        else if (Is5G != BestIs5G)
        {
            Better = Is5G;
        }
        else
        {
            Better = (Bss->Rssi > BestRssi);
        }

        if (Better)
        {
            Best = Chspec;
            BestRssi = Bss->Rssi;
            BestIs5G = Is5G;
        }
    }

    return Best;
}

static
NTSTATUS
CywCipherInfo(
    _In_ ULONG Cipher,
    _Out_ PULONG Wsec,
    _Out_ PUCHAR Suite)
{
    switch (Cipher)
    {
        case DOT11_CIPHER_ALGO_NONE:
            *Wsec = CYW_WSEC_NONE;
            *Suite = 0;
            return STATUS_SUCCESS;
        case DOT11_CIPHER_ALGO_WEP40:
            *Wsec = CYW_WSEC_WEP;
            *Suite = 1;
            return STATUS_SUCCESS;
        case DOT11_CIPHER_ALGO_TKIP:
            *Wsec = CYW_WSEC_TKIP;
            *Suite = 2;
            return STATUS_SUCCESS;
        case DOT11_CIPHER_ALGO_CCMP:
            *Wsec = CYW_WSEC_AES;
            *Suite = 4;
            return STATUS_SUCCESS;
        case DOT11_CIPHER_ALGO_WEP104:
            *Wsec = CYW_WSEC_WEP;
            *Suite = 5;
            return STATUS_SUCCESS;
        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static
BOOLEAN
CywIsWepCipher(
    _In_ ULONG Cipher)
{
    return Cipher == DOT11_CIPHER_ALGO_WEP40 ||
           Cipher == DOT11_CIPHER_ALGO_WEP104;
}

static
BOOLEAN
CywIsWpaCipher(
    _In_ ULONG Cipher)
{
    return Cipher == DOT11_CIPHER_ALGO_TKIP ||
           Cipher == DOT11_CIPHER_ALGO_CCMP;
}

typedef struct _CYW_CONNECT_SETTINGS
{
    UCHAR Ssid[DOT11_SSID_MAX_LENGTH];
    ULONG SsidLength;
    UCHAR Bssid[CYW_ADDRESS_LENGTH];
    BOOLEAN HasBssid;
    ULONG AuthAlgorithm;
    ULONG UnicastCipher;
    ULONG MulticastCipher;
    UCHAR SaePassword[CYW_SAE_PASSWORD_MAX];
    ULONG SaePasswordLength;
    USHORT JoinChanspec;
} CYW_CONNECT_SETTINGS, *PCYW_CONNECT_SETTINGS;

NTSTATUS
CywConnect(
    _In_ PCYW_ADAPTER Adapter)
{
    CYW_EXT_JOIN_PARAMS_LE ExtJoin;
    CYW_JOIN_PARAMS Join;
    ULONG Value;
    ULONG PairwiseWsec;
    ULONG GroupWsec;
    ULONG Wsec;
    ULONG WpaAuth;
    USHORT JoinChanspec;
    ULONG JoinSize;
    UCHAR PairwiseSuite;
    UCHAR GroupSuite;
    BOOLEAN Sae;
    CYW_CONNECT_SETTINGS Settings;
    NTSTATUS Status;

    RtlZeroMemory(&Settings, sizeof(Settings));
    NdisAcquireSpinLock(&Adapter->Lock);
    Settings.SsidLength = Adapter->DesiredSsidLength;
    if (Settings.SsidLength <= DOT11_SSID_MAX_LENGTH)
    {
        RtlCopyMemory(Settings.Ssid, Adapter->DesiredSsid,
                      Settings.SsidLength);
    }
    Settings.HasBssid = Adapter->HasDesiredBssid;
    RtlCopyMemory(Settings.Bssid, Adapter->DesiredBssid,
                  CYW_ADDRESS_LENGTH);
    Settings.AuthAlgorithm = Adapter->AuthAlgorithm;
    Settings.UnicastCipher = Adapter->UnicastCipher;
    Settings.MulticastCipher = Adapter->MulticastCipher;
    Settings.SaePasswordLength = Adapter->SaePasswordLen;
    if (Settings.SaePasswordLength <= CYW_SAE_PASSWORD_MAX)
    {
        RtlCopyMemory(Settings.SaePassword, Adapter->SaePassword,
                      Settings.SaePasswordLength);
    }
    if (Settings.SsidLength != 0 &&
        Settings.SsidLength <= DOT11_SSID_MAX_LENGTH)
    {
        Settings.JoinChanspec = CywSelectJoinChanspecLocked(
            Adapter,
            Settings.Ssid,
            Settings.SsidLength,
            Settings.Bssid,
            Settings.HasBssid);
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    Sae = (Settings.AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE);
    if (Settings.SsidLength == 0 ||
        Settings.SsidLength > DOT11_SSID_MAX_LENGTH ||
        Settings.SaePasswordLength > CYW_SAE_PASSWORD_MAX ||
        (Sae && Settings.SaePasswordLength == 0))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    Status = CywCipherInfo(Settings.UnicastCipher,
                           &PairwiseWsec,
                           &PairwiseSuite);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }
    Status = CywCipherInfo(Settings.MulticastCipher,
                           &GroupWsec,
                           &GroupSuite);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    switch (Settings.AuthAlgorithm)
    {
        case DOT11_AUTH_ALGO_80211_OPEN:
            if (!((Settings.UnicastCipher == DOT11_CIPHER_ALGO_NONE &&
                   Settings.MulticastCipher == DOT11_CIPHER_ALGO_NONE) ||
                  (CywIsWepCipher(Settings.UnicastCipher) &&
                   CywIsWepCipher(Settings.MulticastCipher))))
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            WpaAuth = CYW_WPA_AUTH_DISABLED;
            break;
        case DOT11_AUTH_ALGO_WPA_PSK:
            if (!CywIsWpaCipher(Settings.UnicastCipher) ||
                !CywIsWpaCipher(Settings.MulticastCipher))
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            WpaAuth = CYW_WPA_AUTH_PSK;
            break;
        case DOT11_AUTH_ALGO_RSNA_PSK:
            if (!CywIsWpaCipher(Settings.UnicastCipher) ||
                !CywIsWpaCipher(Settings.MulticastCipher))
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            WpaAuth = CYW_WPA2_AUTH_PSK;
            break;
        case DOT11_AUTH_ALGO_WPA3_SAE:
            if (Settings.UnicastCipher != DOT11_CIPHER_ALGO_CCMP ||
                Settings.MulticastCipher != DOT11_CIPHER_ALGO_CCMP ||
                Settings.SaePasswordLength == 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            WpaAuth = CYW_WPA3_AUTH_SAE_PSK;
            break;
        default:
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
    }
    Wsec = PairwiseWsec | GroupWsec;

    Value = Wsec;
    Status = CywFilCmdSet(Adapter, BRCMF_C_SET_WSEC, &Value, sizeof(Value));
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Value = 1;
    Status = CywFilCmdSet(Adapter, BRCMF_C_SET_INFRA, &Value, sizeof(Value));
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Value = Sae ? CYW_AUTH_SAE : CYW_AUTH_OPEN;
    Status = CywFilCmdSet(Adapter, BRCMF_C_SET_AUTH, &Value, sizeof(Value));
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Value = WpaAuth;
    Status = CywFilCmdSet(Adapter, BRCMF_C_SET_WPA_AUTH, &Value, sizeof(Value));
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Status = CywFilIovarSetInt(Adapter, "mfp",
                               Sae ? CYW_MFP_REQUIRED : CYW_MFP_NONE);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Status = CywFilIovarSetInt(Adapter, "sup_wpa", Sae ? 1 : 0);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    if (Settings.AuthAlgorithm == DOT11_AUTH_ALGO_WPA_PSK)
    {
        UCHAR Ie[24] =
        {
            0xDD, 0x16, 0x00, 0x50, 0xF2, 0x01, 0x01, 0x00,
            0x00, 0x50, 0xF2, 0x00, 0x01, 0x00, 0x00, 0x50,
            0xF2, 0x00, 0x01, 0x00, 0x00, 0x50, 0xF2, 0x02
        };

        Ie[11] = GroupSuite;
        Ie[17] = PairwiseSuite;
        Status = CywFilIovarSet(Adapter, "wpaie", Ie, sizeof(Ie));
        if (!NT_SUCCESS(Status))
        {
            goto Exit;
        }
    }
    else if (Settings.AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK)
    {
        UCHAR Ie[22] =
        {
            0x30, 0x14, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x00,
            0x01, 0x00, 0x00, 0x0F, 0xAC, 0x00, 0x01, 0x00,
            0x00, 0x0F, 0xAC, 0x02, 0x00, 0x00
        };

        Ie[7] = GroupSuite;
        Ie[13] = PairwiseSuite;
        Status = CywFilIovarSet(Adapter, "wpaie", Ie, sizeof(Ie));
        if (!NT_SUCCESS(Status))
        {
            goto Exit;
        }
    }
    else
    {
        /* Open/WEP has no association security IE.  SAE is firmware-owned;
         * leaving a host-built RSNE installed conflicts with that offload. */
        Status = CywFilIovarSet(Adapter, "wpaie", NULL, 0);
        if (!NT_SUCCESS(Status))
        {
            goto Exit;
        }
    }

    if (Sae)
    {
        CYW_WSEC_PMK_LE Pmk;

        RtlZeroMemory(&Pmk, sizeof(Pmk));
        Pmk.KeyLen = (USHORT)Settings.SaePasswordLength;
        Pmk.Flags = CYW_WSEC_PASSPHRASE;
        RtlCopyMemory(Pmk.Key, Settings.SaePassword,
                      Settings.SaePasswordLength);
        Status = CywFilCmdSet(Adapter, BRCMF_C_SET_WSEC_PMK, &Pmk, sizeof(Pmk));
        RtlSecureZeroMemory(&Pmk, sizeof(Pmk));
        if (!NT_SUCCESS(Status))
        {
            goto Exit;
        }
    }

    CywFilIovarSetInt(Adapter, "roam_off", 1);

    {
        ULONG Pm = 0;
        CywFilCmdSet(Adapter, BRCMF_C_SET_PM, &Pm, sizeof(Pm));
    }

    JoinChanspec = Settings.JoinChanspec;

    /* Preferred: firmware-directed join carrying the target BSSID and tuned
     * dwell times ("join" iovar); fall back to the plain SET_SSID join. */
    RtlZeroMemory(&ExtJoin, sizeof(ExtJoin));
    ExtJoin.Ssid.SsidLen = Settings.SsidLength;
    RtlCopyMemory(ExtJoin.Ssid.Ssid, Settings.Ssid,
                  Settings.SsidLength);
    ExtJoin.Scan.ScanType = (UCHAR)-1;
    ExtJoin.Scan.NProbes = CYW_JOIN_ACTIVE_DWELL_MS / CYW_JOIN_PROBE_INTERVAL_MS;
    ExtJoin.Scan.ActiveTime = CYW_JOIN_ACTIVE_DWELL_MS;
    ExtJoin.Scan.PassiveTime = CYW_JOIN_PASSIVE_DWELL_MS;
    ExtJoin.Scan.HomeTime = -1;
    if (Settings.HasBssid)
        RtlCopyMemory(ExtJoin.Assoc.Bssid, Settings.Bssid, CYW_ADDRESS_LENGTH);
    else
        RtlFillMemory(ExtJoin.Assoc.Bssid, CYW_ADDRESS_LENGTH, 0xFF);

    JoinSize = FIELD_OFFSET(CYW_EXT_JOIN_PARAMS_LE, Assoc) + FIELD_OFFSET(CYW_ASSOC_PARAMS_LE, ChanspecList);
    if (JoinChanspec != 0)
    {
        ExtJoin.Assoc.ChanspecNum = 1;
        ExtJoin.Assoc.ChanspecList[0] = JoinChanspec;
        JoinSize += sizeof(USHORT);
    }

    Status = CywFilIovarSet(Adapter, "join", &ExtJoin, JoinSize);
    if (!NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Join, sizeof(Join));
        Join.Ssid.SsidLen = Settings.SsidLength;
        RtlCopyMemory(Join.Ssid.Ssid, Settings.Ssid,
                      Settings.SsidLength);
        if (Settings.HasBssid)
            RtlCopyMemory(Join.Assoc.Bssid, Settings.Bssid, CYW_ADDRESS_LENGTH);
        else
            RtlFillMemory(Join.Assoc.Bssid, CYW_ADDRESS_LENGTH, 0xFF);
        JoinSize = sizeof(Join.Ssid);
        if (JoinChanspec != 0)
        {
            Join.Assoc.ChanspecNum = 1;
            Join.Assoc.ChanspecList[0] = JoinChanspec;
            JoinSize += sizeof(Join.Assoc);
        }
        Status = CywFilCmdSet(Adapter, BRCMF_C_SET_SSID, &Join, JoinSize);
    }

Exit:
    RtlSecureZeroMemory(&Settings, sizeof(Settings));
    return Status;
}

NTSTATUS
CywDisconnect(
    _In_ PCYW_ADAPTER Adapter)
{
    ULONG Value = 0;

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Associated = FALSE;
    Adapter->LinkUp = FALSE;
    Adapter->ConnectedChannelFrequency = 0;
    Adapter->ConnectedRssi = 0;
    Adapter->CurrentRateUnits500Kbps = 0;
    NdisReleaseSpinLock(&Adapter->Lock);

    return CywFilCmdSet(Adapter, BRCMF_C_DISASSOC, &Value, sizeof(Value));
}

ULONG
CywChannelToFrequency(
    _In_ ULONG Channel)
{
    if (Channel == 14)
    {
        return 2484;
    }
    if (Channel >= 1 && Channel <= 13)
    {
        return 2407 + Channel * 5;
    }
    if (Channel >= 36 && Channel <= CYW_CHAN_MAX_5G)
    {
        return 5000 + Channel * 5;
    }
    return 0;
}

static
VOID
CywAddEscanResult(
    _In_ PCYW_ADAPTER Adapter,
    _In_reads_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    PCYW_ESCAN_RESULT_LE Result;
    PCYW_BSS_INFO_LE Bi;
    PCYW_BSS Entry;
    BOOLEAN NewEntry;
    ULONG SsidLen;
    ULONG i;

    if (Length < FIELD_OFFSET(CYW_ESCAN_RESULT_LE, BssInfo) + sizeof(CYW_BSS_INFO_LE))
    {
        return;
    }
    Result = (PCYW_ESCAN_RESULT_LE)Data;
    if (Result->BssCount < 1)
    {
        return;
    }
    Bi = &Result->BssInfo[0];
    if (Bi->Length < sizeof(CYW_BSS_INFO_LE) ||
        Bi->Length > Length - FIELD_OFFSET(CYW_ESCAN_RESULT_LE, BssInfo))
    {
        return;
    }

    NdisAcquireSpinLock(&Adapter->Lock);

    /* Refresh in place when this BSSID was already seen on another channel */
    Entry = NULL;
    NewEntry = FALSE;
    for (i = 0; i < Adapter->BssCount; i++)
    {
        if (RtlEqualMemory(Adapter->Bss[i].Bssid, Bi->Bssid, CYW_ADDRESS_LENGTH))
        {
            Entry = &Adapter->Bss[i];
            break;
        }
    }
    if (Entry == NULL)
    {
        if (Adapter->BssCount >= CYW_MAX_BSS)
        {
            NdisReleaseSpinLock(&Adapter->Lock);
            return;
        }
        Entry = &Adapter->Bss[Adapter->BssCount++];
        RtlZeroMemory(Entry, sizeof(*Entry));
        NewEntry = TRUE;
    }

    RtlCopyMemory(Entry->Bssid, Bi->Bssid, CYW_ADDRESS_LENGTH);

    /* A hidden-SSID beacon must not wipe the SSID a probe response gave us */
    SsidLen = Bi->SsidLen;
    if (SsidLen > DOT11_SSID_MAX_LENGTH)
    {
        SsidLen = DOT11_SSID_MAX_LENGTH;
    }
    if (NewEntry || SsidLen != 0)
    {
        RtlCopyMemory(Entry->Ssid, Bi->Ssid, SsidLen);
        Entry->SsidLength = SsidLen;
    }

    Entry->BssType = dot11_BSS_type_infrastructure;
    Entry->Rssi = Bi->Rssi;
    Entry->LinkQuality = (Entry->Rssi >= -50) ? 100 :
                         (Entry->Rssi <= -100) ? 0 : (2 * (Entry->Rssi + 100));
    Entry->ChannelNumber = Bi->CtlCh ? Bi->CtlCh : (Bi->Chanspec & 0xFF);
    Entry->ChCenterFrequency = CywChannelToFrequency(Entry->ChannelNumber);
    Entry->Chanspec = Bi->Chanspec;
    Entry->CapabilityInformation = Bi->Capability;
    Entry->BeaconPeriod = Bi->BeaconPeriod;

    /* Keep the real beacon/probe IEs, truncated at an element boundary */
    if (Bi->Version == CYW_BSS_INFO_VERSION &&
        Bi->IeOffset >= sizeof(CYW_BSS_INFO_LE) &&
        Bi->IeLength <= Bi->Length &&
        Bi->IeOffset <= Bi->Length - Bi->IeLength)
    {
        PUCHAR Ie = (PUCHAR)Bi + Bi->IeOffset;
        ULONG Take = 0;

        while (Take + 2 <= Bi->IeLength)
        {
            ULONG ElemLen = 2 + Ie[Take + 1];
            if (Take + ElemLen > Bi->IeLength || Take + ElemLen > CYW_MAX_BSS_IE)
            {
                break;
            }
            Take += ElemLen;
        }
        if (Take != 0)
        {
            RtlCopyMemory(Entry->IeBlob, Ie, Take);
            Entry->IeLength = Take;
        }
    }

    NdisReleaseSpinLock(&Adapter->Lock);
}

VOID
CywProcessEvent(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Frame,
    _In_ ULONG Length)
{
    PCYW_BCDC_HEADER Bcdc;
    PCYW_ETHER_HEADER Eth;
    PCYW_BCM_ETH_HEADER Bcm;
    PCYW_EVENT_MSG Msg;
    ULONG HeaderLen;
    ULONG EventType;
    ULONG EventStatus;
    ULONG DataLen;
    ULONG Remaining;

    if (InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        return;
    }

    /* BCDC header, then the 802.3 event frame at the encoded data offset */
    if (Length < BCDC_HEADER_LEN)
    {
        return;
    }
    Bcdc = (PCYW_BCDC_HEADER)Frame;
    HeaderLen = BCDC_HEADER_LEN + ((ULONG)Bcdc->DataOffset << 2);
    if (Length < HeaderLen + sizeof(CYW_ETHER_HEADER) +
                 sizeof(CYW_BCM_ETH_HEADER) + sizeof(CYW_EVENT_MSG))
    {
        return;
    }

    Eth = (PCYW_ETHER_HEADER)(Frame + HeaderLen);
    if (RtlUshortByteSwap(Eth->Type) != ETH_P_LINK_CTL)
    {
        return;
    }

    Bcm = (PCYW_BCM_ETH_HEADER)(Eth + 1);
    if (RtlUshortByteSwap(Bcm->UsrSubtype) != BCMILCP_BCM_SUBTYPE_EVENT)
    {
        return;
    }

    Msg = (PCYW_EVENT_MSG)(Bcm + 1);
    EventType = RtlUlongByteSwap(Msg->EventType);
    EventStatus = RtlUlongByteSwap(Msg->Status);

    Remaining = Length - HeaderLen - sizeof(CYW_ETHER_HEADER) -
                sizeof(CYW_BCM_ETH_HEADER) - sizeof(CYW_EVENT_MSG);
    DataLen = RtlUlongByteSwap(Msg->DataLen);
    if (Remaining > DataLen)
    {
        Remaining = DataLen;
    }

    if (EventType == BRCMF_E_ESCAN_RESULT)
    {
        if (EventStatus == BRCMF_E_STATUS_PARTIAL)
        {
            CywAddEscanResult(Adapter, (PUCHAR)(Msg + 1), Remaining);
        }
        else
        {
            CywIndicateScanComplete(Adapter,
                (EventStatus == BRCMF_E_STATUS_SUCCESS) ? NDIS_STATUS_SUCCESS
                                                        : NDIS_STATUS_FAILURE);
        }
    }
    else if (EventType == BRCMF_E_LINK)
    {
        USHORT LinkFlags = RtlUshortByteSwap(Msg->Flags);
        BOOLEAN WasUp;

        if (LinkFlags & BRCMF_EVENT_MSG_LINK)
        {
            ULONG i;

            NdisAcquireSpinLock(&Adapter->Lock);
            WasUp = Adapter->LinkUp;
            RtlCopyMemory(Adapter->ConnectedBssid, Msg->Addr, CYW_ADDRESS_LENGTH);
            Adapter->ConnectedChannelFrequency = 0;
            Adapter->ConnectedRssi = 0;
            Adapter->CurrentRateUnits500Kbps = 0;
            for (i = 0; i < Adapter->BssCount; i++)
            {
                if (RtlEqualMemory(Adapter->Bss[i].Bssid, Msg->Addr,
                                   CYW_ADDRESS_LENGTH))
                {
                    Adapter->ConnectedChannelFrequency = Adapter->Bss[i].ChCenterFrequency;
                    Adapter->ConnectedRssi = Adapter->Bss[i].Rssi;
                    break;
                }
            }
            Adapter->Associated = TRUE;
            Adapter->LinkUp = TRUE;
            NdisReleaseSpinLock(&Adapter->Lock);
            CywCompletePendingConnect(Adapter, NDIS_STATUS_SUCCESS);
            if (!WasUp)
            {
                /* Link speed and RSSI need firmware round-trips that cannot
                 * run on the bus thread; a work item indicates link state
                 * and link quality with live values. */
                CywQueueLinkUpWork(Adapter);
            }
        }
        else
        {
            NdisAcquireSpinLock(&Adapter->Lock);
            WasUp = Adapter->LinkUp;
            Adapter->Associated = FALSE;
            Adapter->LinkUp = FALSE;
            Adapter->ConnectedChannelFrequency = 0;
            Adapter->ConnectedRssi = 0;
            Adapter->CurrentRateUnits500Kbps = 0;
            NdisReleaseSpinLock(&Adapter->Lock);
            if (WasUp)
            {
                CywIndicateDisassociation(Adapter);
                CywIndicateLinkState(Adapter, FALSE, 0);
            }
        }
    }
    else if (EventType == BRCMF_E_SET_SSID)
    {
        if (EventStatus != BRCMF_E_STATUS_SUCCESS)
        {
            if (InterlockedIncrement(&Adapter->JoinRetries) <= 2 && Adapter->DesiredSsidLength != 0 && !Adapter->Halting)
            {
                DPRINT1("CYW: join failed status %lu, retry %ld\n", EventStatus, Adapter->JoinRetries);
                CywQueueConnectWork(Adapter);
            }
            else
            {
                CywCompletePendingConnect(Adapter, NDIS_STATUS_FAILURE);
            }
        }
        else
        {
            InterlockedExchange(&Adapter->JoinRetries, 0);
        }
    }
}

PNET_BUFFER_LIST
CywRxData(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Body,
    _In_ ULONG BodyLen)
{
    PCYW_BCDC_HEADER Bcdc;
    PCYW_ETHER_HEADER EthHdr;
    PUCHAR Pay;
    ULONG Off;
    ULONG PayLen;
    ULONG FrameLen;
    PUCHAR Frame;
    PCYW_DOT11_HEADER Dot11;
    PCYW_SNAP_HEADER Snap;
    PMDL Mdl;
    PNET_BUFFER_LIST Nbl;
    PCYW_RX_BUF Rb;
    DOT11_MAC_ADDRESS Bssid;
    ULONG ChannelFrequency;
    LONG Rssi;
    ULONG RateUnits500Kbps;

    if (InterlockedCompareExchange(&Adapter->Paused, 0, 0) != 0 ||
        InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        return NULL;
    }

    if (BodyLen < BCDC_HEADER_LEN)
    {
        return NULL;
    }

    Bcdc = (PCYW_BCDC_HEADER)Body;
    Off = BCDC_HEADER_LEN + ((ULONG)Bcdc->DataOffset << 2);
    if (Off + sizeof(CYW_ETHER_HEADER) > BodyLen)
    {
        return NULL;
    }

    Pay = Body + Off;
    PayLen = BodyLen - Off;
    EthHdr = (PCYW_ETHER_HEADER)Pay;

    FrameLen = sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER) +
               (PayLen - sizeof(CYW_ETHER_HEADER));
    if (FrameLen > CYW_MAX_FRAME_SIZE)
    {
        return NULL;
    }

    Rb = CywAcquireRxBuffer(Adapter);
    if (Rb == NULL)
    {
        return NULL;
    }
    Frame = Rb->Buffer;

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlCopyMemory(Bssid, Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
    ChannelFrequency = Adapter->ConnectedChannelFrequency;
    Rssi = Adapter->ConnectedRssi;
    RateUnits500Kbps = Adapter->CurrentRateUnits500Kbps;
    NdisReleaseSpinLock(&Adapter->Lock);

    Dot11 = (PCYW_DOT11_HEADER)Frame;
    RtlZeroMemory(Dot11, sizeof(CYW_DOT11_HEADER));
    Dot11->FrameControl[0] = CYW_FC0_TYPE_DATA;
    Dot11->FrameControl[1] = CYW_FC1_FROMDS;
    /* In a From-DS frame Address1 is the original Ethernet destination.  Keep
     * multicast and broadcast destinations intact instead of presenting every
     * received frame to Native Wi-Fi as directed to the station. */
    RtlCopyMemory(Dot11->Address1, EthHdr->Dest, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(Dot11->Address2, Bssid, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(Dot11->Address3, EthHdr->Src, CYW_ADDRESS_LENGTH);

    Snap = (PCYW_SNAP_HEADER)(Frame + sizeof(CYW_DOT11_HEADER));
    Snap->Dsap = CYW_SNAP_DSAP;
    Snap->Ssap = CYW_SNAP_SSAP;
    Snap->Control = CYW_SNAP_CONTROL;
    Snap->Oui[0] = 0;
    Snap->Oui[1] = 0;
    {
        USHORT EtherType = RtlUshortByteSwap(EthHdr->Type);
        Snap->Oui[2] = (EtherType == ETH_P_AARP || EtherType == ETH_P_IPX)
                           ? CYW_SNAP_OUI_BRIDGE_TUNNEL
                           : 0;
    }
    RtlCopyMemory(Snap->EtherType, &EthHdr->Type, sizeof(Snap->EtherType));

    RtlCopyMemory(Frame + sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER),
                  Pay + sizeof(CYW_ETHER_HEADER),
                  PayLen - sizeof(CYW_ETHER_HEADER));

    Mdl = Rb->Mdl;

    Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0, Mdl, 0, FrameLen);
    if (Nbl == NULL)
    {
        CywReleaseRxBuffer(Adapter, Rb);
        return NULL;
    }

    Nbl->MiniportReserved[0] = Rb;
    Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
    NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

    RtlZeroMemory(&Rb->RecvContext, sizeof(Rb->RecvContext));
    Rb->RecvContext.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Rb->RecvContext.Header.Revision = DOT11_EXTSTA_RECV_CONTEXT_REVISION_1;
    Rb->RecvContext.Header.Size = sizeof(DOT11_EXTSTA_RECV_CONTEXT);
    Rb->RecvContext.uReceiveFlags = 0;
    Rb->RecvContext.uPhyId = 0;
    Rb->RecvContext.uChCenterFrequency = ChannelFrequency;
    Rb->RecvContext.usNumberOfMPDUsReceived = 1;
    Rb->RecvContext.lRSSI = Rssi;
    Rb->RecvContext.ucDataRate = CywDataRateIndexFromUnits(RateUnits500Kbps);
    NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = &Rb->RecvContext;

    if (InterlockedCompareExchange(&Adapter->Paused, 0, 0) != 0 ||
        InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        NdisFreeNetBufferList(Nbl);
        CywReleaseRxBuffer(Adapter, Rb);
        return NULL;
    }

    InterlockedIncrement64(&Adapter->RxOkCount);
    return Nbl;
}

static
VOID
CywClearChipInterrupt(
    _In_ PCYW_ADAPTER Adapter)
{
    ULONG Ist = 0;

    if (Adapter->SdioCoreBase == 0)
    {
        return;
    }

    if (!NT_SUCCESS(CywBackplaneReadlSc(Adapter,
                                        Adapter->SdioCoreBase + SD_REG_INTSTATUS,
                                        &Ist, Adapter->RegScratch)))
    {
        return;
    }

    Ist &= CYW_HOSTINTMASK;
    if (Ist != 0)
    {
        CywBackplaneWritelSc(Adapter, Adapter->SdioCoreBase + SD_REG_INTSTATUS,
                             Ist, Adapter->RegScratch);
        if (Ist & I_HMB_HOST_INT)
        {
            ULONG Hmb = 0;
            CywBackplaneReadlSc(Adapter,
                                Adapter->SdioCoreBase + SD_REG_TOHOSTMAILBOXDATA,
                                &Hmb, Adapter->RegScratch);
            CywBackplaneWritelSc(Adapter,
                                 Adapter->SdioCoreBase + SD_REG_TOSBMAILBOX,
                                 SMB_INT_ACK, Adapter->RegScratch);
        }
    }
}

static
VOID
CywRxChainAppend(
    _Inout_ PNET_BUFFER_LIST *Head,
    _Inout_ PNET_BUFFER_LIST *Tail,
    _Inout_ PULONG Count,
    _In_opt_ PNET_BUFFER_LIST Nbl)
{
    if (Nbl == NULL)
    {
        return;
    }
    if (*Tail != NULL)
    {
        NET_BUFFER_LIST_NEXT_NBL(*Tail) = Nbl;
    }
    else
    {
        *Head = Nbl;
    }
    *Tail = Nbl;
    (*Count)++;
}

static
VOID
CywRxChainFlush(
    _In_ PCYW_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST *Head,
    _Inout_ PNET_BUFFER_LIST *Tail,
    _Inout_ PULONG Count)
{
    if (*Head == NULL)
    {
        return;
    }
    if (InterlockedCompareExchange(&Adapter->Paused, 0, 0) != 0 ||
        InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        CywMiniportReturnNetBufferLists(Adapter, *Head, 0);
    }
    else
    {
        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle, *Head,
                                           NDIS_DEFAULT_PORT_NUMBER, *Count, 0);
    }
    *Head = NULL;
    *Tail = NULL;
    *Count = 0;
}

static
VOID
CywSdioAbort(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function)
{
    CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_IOABORT, Function);
}

static
VOID
CywRxFail(
    _In_ PCYW_ADAPTER Adapter,
    _In_ BOOLEAN Abort,
    _In_ BOOLEAN SendNak)
{
    ULONG Retries;
    UCHAR Hi = 0;
    UCHAR Lo = 0;

    Adapter->RxFailCount++;

    if (Abort)
    {
        CywSdioAbort(Adapter, CYW_SDIO_FUNC_RADIO);
    }

    CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE, SBSDIO_FUNC1_FRAMECTRL, SFC_RF_TERM);

    for (Retries = CYW_RXFLUSH_RETRIES; Retries > 0; Retries--)
    {
        if (!NT_SUCCESS(CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BACKPLANE, SBSDIO_FUNC1_RFRAMEBCHI, &Hi)) || !NT_SUCCESS(CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BACKPLANE, SBSDIO_FUNC1_RFRAMEBCLO, &Lo)))
        {
            break;
        }

        if (Hi == 0 && Lo == 0)
        {
            break;
        }
    }

    if (Retries == 0)
    {
        Adapter->RxFlushStuckCount++;
    }

    if (SendNak && Adapter->SdioCoreBase != 0)
    {
        CywBackplaneWritelSc(Adapter, Adapter->SdioCoreBase + SD_REG_TOSBMAILBOX, SMB_NAK, Adapter->RegScratch);
    }

    Adapter->RxSeqValid = FALSE;
}

/* Single owner of all F2 FIFO traffic: pumps the deferred TX queue, receives
 * frames, and dispatches control responses, firmware events and RX data.
 * Woken by the card interrupt, by TX submission, or to exit. */
static
VOID
NTAPI
CywBusThread(
    _In_ PVOID Context)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)Context;
    PUCHAR Frame = Adapter->RxBuffer;
    NTSTATUS Status;
    ULONG FrameLen;
    ULONG HwLen;
    ULONG HwCheck;
    UCHAR RxSeqNum;
    UCHAR Channel;
    ULONG DataOffset;
    PNET_BUFFER_LIST ChainHead = NULL, ChainTail = NULL;
    ULONG ChainCount = 0;

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    while (Adapter->BusThreadStop == 0)
    {
        BOOLEAN AnyFrame = FALSE;
        BOOLEAN HadInterrupt;
        LARGE_INTEGER RxWait;
        ULONG NextLen = 0;
        ULONG FirstRead;
        ULONG RxLeft = CYW_RXBOUND;
        ULONG SwOff = 4;

        HadInterrupt =
            (InterlockedExchange(&Adapter->CardInterruptPending, 0) != 0);
        if (HadInterrupt)
        {
            CywClearChipInterrupt(Adapter);
        }

        CywDrainTxQueue(Adapter);

        while (Adapter->BusThreadStop == 0 && RxLeft-- != 0)
        {
            BOOLEAN ReadAhead = (NextLen != 0);

            KeWaitForSingleObject(&Adapter->F2Lock, Executive, KernelMode, FALSE, NULL);

            FirstRead = (NextLen != 0) ? NextLen : CYW_FIRSTREAD;
            if (FirstRead > CYW_RX_BUFFER_SIZE - CYW_F2_BLOCKSIZE)
            {
                FirstRead = CYW_FIRSTREAD;
                NextLen = 0;
                ReadAhead = FALSE;
            }

            Status = CywSdpcmF2Fifo(Adapter, FALSE, Frame, FirstRead);
            if (!NT_SUCCESS(Status))
            {
                NextLen = 0;
                CywRxFail(Adapter, TRUE, FALSE);
                KeReleaseMutex(&Adapter->F2Lock, FALSE);
                break;
            }

            HwLen = Frame[0] | (Frame[1] << 8);
            HwCheck = Frame[2] | (Frame[3] << 8);

            if (HwLen == 0 && HwCheck == 0)
            {
                NextLen = 0;
                KeReleaseMutex(&Adapter->F2Lock, FALSE);
                break;
            }

            if (((HwLen ^ HwCheck) & 0xFFFF) != 0xFFFF)
            {
                Adapter->RxBadHdrCount++;
                NextLen = 0;
                CywRxFail(Adapter, FALSE, FALSE);
                KeReleaseMutex(&Adapter->F2Lock, FALSE);
                break;
            }

            FrameLen = HwLen;
            if (FrameLen < SDPCM_HEADER_LEN || FrameLen > CYW_RX_BUFFER_SIZE - CYW_F2_BLOCKSIZE)
            {
                Adapter->RxBadHdrCount++;
                NextLen = 0;
                CywRxFail(Adapter, FALSE, FALSE);
                KeReleaseMutex(&Adapter->F2Lock, FALSE);
                break;
            }

            /* The firmware's next-length byte is a 16-byte-rounded readahead
             * promise.  Reading a different-sized frame with that promise
             * means the FIFO is no longer at the boundary we expected. */
            if (ReadAhead && ALIGN_UP(FrameLen, 16) != FirstRead)
            {
                Adapter->RxBadHdrCount++;
                NextLen = 0;
                CywRxFail(Adapter, TRUE, TRUE);
                KeReleaseMutex(&Adapter->F2Lock, FALSE);
                break;
            }

            NextLen = (ULONG)Frame[SwOff + 2] << 4;

            RxSeqNum = Frame[SwOff];
            if (Adapter->RxSeqValid && RxSeqNum != Adapter->RxSeq)
            {
                Adapter->RxBadSeqCount++;
            }
            Adapter->RxSeq = (UCHAR)(RxSeqNum + 1);
            Adapter->RxSeqValid = TRUE;

            CywUpdateCredits(Adapter, Frame);

            if (FrameLen > FirstRead)
            {
                Status = CywSdpcmF2Fifo(Adapter, FALSE, Frame + FirstRead, FrameLen - FirstRead);
                if (!NT_SUCCESS(Status))
                {
                    NextLen = 0;
                    CywRxFail(Adapter, TRUE, FALSE);
                    KeReleaseMutex(&Adapter->F2Lock, FALSE);
                    break;
                }
            }

            KeReleaseMutex(&Adapter->F2Lock, FALSE);
            AnyFrame = TRUE;
            CywDrainTxQueue(Adapter);

            Channel = Frame[SwOff + 1] & 0x0F;
            DataOffset = Frame[SwOff + 3];
            if (DataOffset < SDPCM_HEADER_LEN || DataOffset >= FrameLen)
            {
                Adapter->RxBadHdrCount++;
                continue;
            }

            if (Frame[SwOff + 1] & SDPCM_GLOMDESC_FLAG)
            {
                ULONG n = 0;
                ULONG p = DataOffset;
                while (p + 2 <= FrameLen && n < RTL_NUMBER_OF(Adapter->GlomLens))
                {
                    Adapter->GlomLens[n] = (USHORT)(Frame[p] | (Frame[p + 1] << 8));
                    n++;
                    p += 2;
                }
                Adapter->GlomCount = n;
                continue;
            }

            if (Channel == SDPCM_CHANNEL_GLOM)
            {
                ULONG Cnt = Adapter->GlomCount;
                ULONG Off = 0;
                ULONG i;
                Adapter->GlomCount = 0;
                for (i = 0; i < Cnt; i++)
                {
                    ULONG SubLen = Adapter->GlomLens[i];
                    ULONG SegmentEnd;
                    ULONG HdrAt;
                    PUCHAR Sub;
                    ULONG OwnLen;
                    UCHAR SubChan;
                    ULONG SubDoff;

                    if (SubLen == 0 || Off > FrameLen ||
                        SubLen > FrameLen - Off)
                    {
                        break;
                    }
                    SegmentEnd = Off + SubLen;
                    HdrAt = Off + ((i == 0) ? DataOffset : 0);
                    Off = SegmentEnd;
                    if (HdrAt >= SegmentEnd ||
                        SDPCM_HEADER_LEN > SegmentEnd - HdrAt)
                    {
                        continue;
                    }
                    Sub = Frame + HdrAt;
                    OwnLen = Sub[0] | (Sub[1] << 8);
                    if (OwnLen < SDPCM_HEADER_LEN ||
                        OwnLen > SegmentEnd - HdrAt ||
                        ((Sub[0] ^ Sub[2]) != 0xFF) ||
                        ((Sub[1] ^ Sub[3]) != 0xFF))
                        continue;
                    SubChan = Sub[SwOff + 1] & 0x0F;
                    SubDoff = Sub[SwOff + 3];
                    if (SubDoff < SDPCM_HEADER_LEN || SubDoff >= OwnLen)
                        continue;
                    if (SubChan == SDPCM_CHANNEL_DATA)
                    {
                        PNET_BUFFER_LIST Nbl = CywRxData(Adapter, Sub + SubDoff, OwnLen - SubDoff);
                        CywRxChainAppend(&ChainHead, &ChainTail, &ChainCount, Nbl);
                    }
                    else if (SubChan == SDPCM_CHANNEL_EVENT)
                    {
                        CywProcessEvent(Adapter, Sub + SubDoff, OwnLen - SubDoff);
                    }
                }
                if (ChainCount >= CYW_RX_INDICATE_BATCH)
                {
                    CywRxChainFlush(Adapter, &ChainHead, &ChainTail, &ChainCount);
                }
                continue;
            }

            if (Channel == SDPCM_CHANNEL_CONTROL)
            {
                ULONG Payload = FrameLen - DataOffset;
                PCYW_BCDC_DCMD Response;
                ULONG ResponseId;
                LONG ExpectedId;
                LONG ExpectedCommand;

                if (Payload < sizeof(CYW_BCDC_DCMD))
                {
                    Adapter->RxBadHdrCount++;
                    continue;
                }

                Response = (PCYW_BCDC_DCMD)(Frame + DataOffset);
                ResponseId = (Response->Flags & BCDC_DCMD_ID_MASK) >>
                             BCDC_DCMD_ID_SHIFT;
                ExpectedId = InterlockedCompareExchange(
                    &Adapter->ExpectedBcdcRequestId, 0, 0);
                ExpectedCommand = InterlockedCompareExchange(
                    &Adapter->ExpectedBcdcCommand, 0, 0);
                if (ExpectedId < 0 || ResponseId != (ULONG)ExpectedId ||
                    Response->Cmd != (ULONG)ExpectedCommand)
                {
                    /* A late response belongs to an already timed-out request. */
                    continue;
                }

                if (InterlockedCompareExchange(
                        &Adapter->ExpectedBcdcRequestId,
                        -1,
                        ExpectedId) != ExpectedId)
                {
                    continue;
                }
                InterlockedExchange(&Adapter->ExpectedBcdcCommand, -1);

                if (Payload > CYW_CONTROL_BUFFER_SIZE)
                {
                    Adapter->CtrlResponseLen = 0;
                    InterlockedExchange(&Adapter->CtrlResponseStatus,
                                        STATUS_BUFFER_OVERFLOW);
                }
                else
                {
                    RtlCopyMemory(Adapter->ControlBuffer,
                                  Frame + DataOffset,
                                  Payload);
                    Adapter->CtrlResponseLen = Payload;
                    InterlockedExchange(&Adapter->CtrlResponseStatus,
                                        STATUS_SUCCESS);
                }
                KeSetEvent(&Adapter->CtrlEvent, IO_NO_INCREMENT, FALSE);
            }
            else if (Channel == SDPCM_CHANNEL_EVENT)
            {
                CywProcessEvent(Adapter, Frame + DataOffset, FrameLen - DataOffset);
            }
            else if (Channel == SDPCM_CHANNEL_DATA)
            {
                PNET_BUFFER_LIST Nbl = CywRxData(Adapter, Frame + DataOffset,
                                                 FrameLen - DataOffset);
                CywRxChainAppend(&ChainHead, &ChainTail, &ChainCount, Nbl);
                if (ChainCount >= CYW_RX_INDICATE_BATCH)
                {
                    CywRxChainFlush(Adapter, &ChainHead, &ChainTail, &ChainCount);
                }
            }
        }

        CywRxChainFlush(Adapter, &ChainHead, &ChainTail, &ChainCount);

        if (AnyFrame)
        {
            if (HadInterrupt && Adapter->SdBus.AcknowledgeInterrupt != NULL)
            {
                Adapter->SdBus.AcknowledgeInterrupt(Adapter->SdBus.Context);
            }
            continue;
        }

        if (HadInterrupt && Adapter->SdBus.AcknowledgeInterrupt != NULL)
        {
            Adapter->SdBus.AcknowledgeInterrupt(Adapter->SdBus.Context);
        }
        RxWait.QuadPart = CYW_RX_POLL_FALLBACK;
        KeWaitForSingleObject(&Adapter->BusEvent, Executive, KernelMode,
                              FALSE, &RxWait);
    }

    KeSetEvent(&Adapter->BusThreadExited, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
CywStartBusThread(
    _In_ PCYW_ADAPTER Adapter)
{
    HANDLE Handle;
    NTSTATUS Status;

    Adapter->BusThreadStop = 0;
    Adapter->BusThread = NULL;
    KeInitializeEvent(&Adapter->BusThreadExited, NotificationEvent, FALSE);

    if (!Adapter->TimerResSet)
    {
        Adapter->TimerResSet = TRUE;
        ExSetTimerResolution(CYW_RX_TIMER_RES, TRUE);
    }

    /* Published before the thread exists: CywBusThread starts draining the F2
     * FIFO the moment it is created, and any control transfer that still sees
     * BusThreadRunning == FALSE would take the unlocked CywSdpcmRecvCtl poll
     * path and read the same FIFO concurrently, desynchronising SDPCM. */
    InterlockedExchange(&Adapter->BusThreadRunning, 1);

    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  CywBusThread, Adapter);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Adapter->BusThreadRunning, 0);
        if (Adapter->TimerResSet)
        {
            Adapter->TimerResSet = FALSE;
            ExSetTimerResolution(0, FALSE);
        }
        return Status;
    }

    Status = ObReferenceObjectByHandle(Handle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, &Adapter->BusThread, NULL);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Adapter->BusThreadStop, 1);
        KeSetEvent(&Adapter->BusEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(&Adapter->BusThreadExited,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        ZwClose(Handle);
        Adapter->BusThread = NULL;
        InterlockedExchange(&Adapter->BusThreadRunning, 0);
        if (Adapter->TimerResSet)
        {
            Adapter->TimerResSet = FALSE;
            ExSetTimerResolution(0, FALSE);
        }
        return Status;
    }
    ZwClose(Handle);
    return Status;
}

VOID
CywStopBusThread(
    _In_ PCYW_ADAPTER Adapter)
{
    if (InterlockedCompareExchange(&Adapter->BusThreadRunning, 0, 0) == 0)
    {
        return;
    }

    InterlockedExchange(&Adapter->BusThreadStop, 1);
    KeSetEvent(&Adapter->BusEvent, IO_NO_INCREMENT, FALSE);

    if (Adapter->BusThread != NULL)
    {
        KeWaitForSingleObject(Adapter->BusThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Adapter->BusThread);
        Adapter->BusThread = NULL;
    }

    InterlockedExchange(&Adapter->BusThreadRunning, 0);

    if (Adapter->TimerResSet)
    {
        Adapter->TimerResSet = FALSE;
        ExSetTimerResolution(0, FALSE);
    }
}
