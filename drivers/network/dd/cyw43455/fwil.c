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
#define SDPCM_SEQ_OFFSET        4
#define SDPCM_CHANNEL_OFFSET    5
#define SDPCM_DOFFSET_OFFSET    7

#define SDIO_F2_FIFO            0x8000

static
NTSTATUS
CywSdpcmSendCtl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Data,
    _In_ ULONG Length)
{
    PUCHAR Frame = Adapter->ControlBuffer;
    ULONG Total = SDPCM_HEADER_LEN + Length;
    ULONG Padded = ALIGN_UP(Total, ULONG);
    NTSTATUS Status;

    if (Padded > CYW_CONTROL_BUFFER_SIZE)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(Frame, SDPCM_HEADER_LEN);
    Frame[0] = (UCHAR)(Total & 0xFF);
    Frame[1] = (UCHAR)((Total >> 8) & 0xFF);
    Frame[2] = (UCHAR)(~Frame[0]);
    Frame[3] = (UCHAR)(~Frame[1]);
    Frame[4] = Adapter->TxSeq;
    Frame[5] = SDPCM_CHANNEL_CONTROL;
    Frame[7] = SDPCM_HEADER_LEN;

    RtlCopyMemory(Frame + SDPCM_HEADER_LEN, Data, Length);

    Status = CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO, Frame, Padded);
    if (NT_SUCCESS(Status))
    {
        Adapter->TxSeq++;
    }
    return Status;
}

static
NTSTATUS
CywSdpcmRecvCtl(
    _In_ PCYW_ADAPTER Adapter,
    _Out_ PUCHAR Data,
    _Inout_ PULONG Length)
{
    PUCHAR Frame = Adapter->ControlBuffer;
    NTSTATUS Status;
    ULONG Retry;
    ULONG FrameLen;
    ULONG DataOffset;
    ULONG Payload;

    for (Retry = 0; Retry < 1000; Retry++)
    {
        Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                  Frame, SDPCM_HEADER_LEN);
        if (NT_SUCCESS(Status))
        {
            FrameLen = Frame[0] | (Frame[1] << 8);
            if (FrameLen >= SDPCM_HEADER_LEN && FrameLen <= CYW_CONTROL_BUFFER_SIZE &&
                ((Frame[0] ^ Frame[2]) == 0xFF))
            {
                DataOffset = Frame[SDPCM_DOFFSET_OFFSET];
                if (DataOffset < FrameLen)
                {
                    UCHAR Channel = Frame[SDPCM_CHANNEL_OFFSET] & 0x0F;
                    if (FrameLen > SDPCM_HEADER_LEN)
                    {
                        Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                                  Frame + SDPCM_HEADER_LEN,
                                                  ALIGN_UP(FrameLen - SDPCM_HEADER_LEN, ULONG));
                        if (!NT_SUCCESS(Status))
                        {
                            KeStallExecutionProcessor(500);
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
                        if (Payload > *Length)
                        {
                            Payload = *Length;
                        }
                        RtlCopyMemory(Data, Frame + DataOffset, Payload);
                        *Length = Payload;
                        return STATUS_SUCCESS;
                    }
                }
            }
        }
        KeStallExecutionProcessor(500);
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
    ULONG MsgLen = sizeof(CYW_BCDC_DCMD) + Length;
    ULONG RecvLen;
    NTSTATUS Status;

    Msg = CywAllocate(MsgLen + sizeof(CYW_BCDC_DCMD));
    if (Msg == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Adapter->BcdcRequestId++;

    Dcmd = (PCYW_BCDC_DCMD)Msg;
    Dcmd->Cmd = Cmd;
    Dcmd->Len = Length;
    Dcmd->Flags = (Adapter->BcdcRequestId << BCDC_DCMD_ID_SHIFT) | BCDC_PROTO_VER;
    if (Set)
    {
        Dcmd->Flags |= BCDC_DCMD_SET;
    }
    Dcmd->Status = 0;

    if (Length > 0 && Data != NULL)
    {
        RtlCopyMemory(Msg + sizeof(CYW_BCDC_DCMD), Data, Length);
    }

    Status = CywSdpcmSendCtl(Adapter, Msg, MsgLen);
    if (!NT_SUCCESS(Status))
    {
        CywFree(Msg);
        return Status;
    }

    RecvLen = MsgLen + sizeof(CYW_BCDC_DCMD);
    Status = CywSdpcmRecvCtl(Adapter, Msg, &RecvLen);
    if (NT_SUCCESS(Status) && RecvLen >= sizeof(CYW_BCDC_DCMD))
    {
        Dcmd = (PCYW_BCDC_DCMD)Msg;
        if (Dcmd->Flags & BCDC_DCMD_ERROR)
        {
            DPRINT1("CYW: BCDC cmd %lu firmware error %d\n", Cmd, (LONG)Dcmd->Status);
            Status = STATUS_UNSUCCESSFUL;
        }
        else if (!Set && Data != NULL)
        {
            ULONG Copy = RecvLen - sizeof(CYW_BCDC_DCMD);
            if (Copy > Length)
            {
                Copy = Length;
            }
            RtlCopyMemory(Data, Msg + sizeof(CYW_BCDC_DCMD), Copy);
        }
    }

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
    ULONG NameLen = (ULONG)strlen(Name) + 1;
    ULONG Total = NameLen + Length;
    NTSTATUS Status;

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
        BRCMF_E_ASSOC, BRCMF_E_DISASSOC, BRCMF_E_LINK, BRCMF_E_IF,
        BRCMF_E_ESCAN_RESULT
    };
    ULONG i;

    RtlZeroMemory(Mask, sizeof(Mask));
    for (i = 0; i < sizeof(Events); i++)
    {
        Mask[Events[i] >> 3] |= (UCHAR)(1u << (Events[i] & 7));
    }

    return CywFilIovarSet(Adapter, "event_msgs", Mask, sizeof(Mask));
}

NTSTATUS
CywScanStart(
    _In_ PCYW_ADAPTER Adapter,
    _In_opt_ PDOT11_SCAN_REQUEST_V2 Request)
{
    PCYW_ESCAN_PARAMS_LE Params;
    ULONG Size = sizeof(CYW_ESCAN_PARAMS_LE);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Request);

    Params = CywAllocate(Size);
    if (Params == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Params->Version = BRCMF_ESCAN_REQ_VERSION;
    Params->Action = WL_ESCAN_ACTION_START;
    Params->SyncId = CYW_ESCAN_SYNCID;

    Params->Params.Ssid.SsidLen = 0;
    RtlFillMemory(Params->Params.Bssid, 6, 0xFF);
    Params->Params.BssType = DOT11_BSSTYPE_ANY;
    Params->Params.ScanType = BRCMF_SCANTYPE_ACTIVE;
    Params->Params.NProbes = -1;
    Params->Params.ActiveTime = -1;
    Params->Params.PassiveTime = -1;
    Params->Params.HomeTime = -1;
    Params->Params.ChannelNum = 0;

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->BssCount = 0;
    Adapter->ScanInProgress = TRUE;
    NdisReleaseSpinLock(&Adapter->Lock);

    Status = CywFilIovarSet(Adapter, "escan", Params, Size);

    CywFree(Params);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: escan start failed 0x%08lx\n", Status);
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->ScanInProgress = FALSE;
        NdisReleaseSpinLock(&Adapter->Lock);
    }

    return Status;
}

static
VOID
CywAddEscanResult(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Bss,
    _In_ ULONG Length)
{
    PCYW_BSS Entry;
    ULONG SsidLen;

    UNREFERENCED_PARAMETER(Length);

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->BssCount >= CYW_MAX_BSS)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return;
    }

    Entry = &Adapter->Bss[Adapter->BssCount];
    RtlZeroMemory(Entry, sizeof(*Entry));

    RtlCopyMemory(Entry->Bssid, Bss + 8, 6);
    SsidLen = Bss[18];
    if (SsidLen > DOT11_SSID_MAX_LENGTH)
    {
        SsidLen = DOT11_SSID_MAX_LENGTH;
    }
    RtlCopyMemory(Entry->Ssid, Bss + 19, SsidLen);
    Entry->SsidLength = SsidLen;
    Entry->BssType = dot11_BSS_type_infrastructure;
    Entry->Rssi = (LONG)(SHORT)(Bss[78] | (Bss[79] << 8));
    Entry->LinkQuality = (Entry->Rssi >= -50) ? 100 :
                         (Entry->Rssi <= -100) ? 0 : (2 * (Entry->Rssi + 100));
    Entry->ChannelNumber = (Bss[72] | (Bss[73] << 8)) & 0xFF;
    Entry->CapabilityInformation = (USHORT)(Bss[16] | (Bss[17] << 8));

    Adapter->BssCount++;
    NdisReleaseSpinLock(&Adapter->Lock);
}

VOID
CywProcessEvent(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Frame,
    _In_ ULONG Length)
{
    PCYW_ETHER_HEADER Eth;
    PCYW_EVENT_MSG Msg;
    ULONG EventType;
    ULONG EventStatus;
    ULONG ScanI;
    ULONG EthAt = 0;
    BOOLEAN Found = FALSE;

    for (ScanI = 0; ScanI + 1 < Length; ScanI++)
    {
        if (Frame[ScanI] == 0x88 && Frame[ScanI + 1] == 0x6C)
        {
            EthAt = ScanI;
            Found = TRUE;
            break;
        }
    }
    if (!Found)
    {
        return;
    }
    if (EthAt < FIELD_OFFSET(CYW_ETHER_HEADER, Type))
    {
        return;
    }
    Frame += EthAt - FIELD_OFFSET(CYW_ETHER_HEADER, Type);
    Length -= EthAt - FIELD_OFFSET(CYW_ETHER_HEADER, Type);

    if (Length < sizeof(CYW_ETHER_HEADER) + 10 + sizeof(CYW_EVENT_MSG))
    {
        return;
    }

    Eth = (PCYW_ETHER_HEADER)Frame;
    Msg = (PCYW_EVENT_MSG)(Frame + sizeof(CYW_ETHER_HEADER) + 10);
    EventType = RtlUlongByteSwap(Msg->EventType);
    EventStatus = RtlUlongByteSwap(Msg->Status);

    if (RtlUshortByteSwap(Eth->Type) != ETH_P_LINK_CTL)
    {
        return;
    }

    if (EventType == BRCMF_E_ESCAN_RESULT)
    {
        if (EventStatus == BRCMF_E_STATUS_PARTIAL)
        {
            PUCHAR Result = (PUCHAR)Msg + sizeof(CYW_EVENT_MSG);
            CywAddEscanResult(Adapter, Result + 12, Length);
        }
        else
        {
            CywIndicateScanComplete(Adapter, NDIS_STATUS_SUCCESS);
        }
    }
}

VOID
CywPollEvents(
    _In_ PCYW_ADAPTER Adapter)
{
    PUCHAR Frame = Adapter->ControlBuffer;
    NTSTATUS Status;
    ULONG Retry;
    ULONG FrameLen;
    UCHAR Channel;
    ULONG DataOffset;
    ULONG Events = 0;

    for (Retry = 0; Retry < 2500; Retry++)
    {
        Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                  Frame, SDPCM_HEADER_LEN);
        FrameLen = Frame[0] | (Frame[1] << 8);
        if (!NT_SUCCESS(Status) || FrameLen < SDPCM_HEADER_LEN ||
            FrameLen > CYW_CONTROL_BUFFER_SIZE || ((Frame[0] ^ Frame[2]) != 0xFF))
        {
            KeStallExecutionProcessor(2000);
            continue;
        }

        Channel = Frame[SDPCM_CHANNEL_OFFSET] & 0x0F;
        DataOffset = Frame[SDPCM_DOFFSET_OFFSET];
        if (FrameLen > SDPCM_HEADER_LEN)
        {
            ULONG Body = ALIGN_UP(FrameLen - SDPCM_HEADER_LEN, ULONG);
            ULONG Done = 0;
            while (Done < Body)
            {
                ULONG Chunk = Body - Done;
                if (Chunk > CYW_F2_BLOCKSIZE)
                {
                    Chunk = CYW_F2_BLOCKSIZE;
                }
                CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                 Frame + SDPCM_HEADER_LEN + Done, Chunk);
                Done += Chunk;
            }
        }

        if (Channel == SDPCM_CHANNEL_EVENT && DataOffset < FrameLen)
        {
            CywProcessEvent(Adapter, Frame + DataOffset, FrameLen - DataOffset);
            Events++;
        }
    }

    DPRINT1("CYW: poll done, %lu events, %lu BSS\n", Events, Adapter->BssCount);
}
