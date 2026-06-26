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
    Frame[5] = SDPCM_CHANNEL_CONTROL;
    Frame[7] = SDPCM_HEADER_LEN;

    RtlCopyMemory(Frame + SDPCM_HEADER_LEN, Data, Length);

    KeWaitForSingleObject(&Adapter->F2Lock, Executive, KernelMode, FALSE, NULL);
    Frame[SDPCM_SEQ_OFFSET] = Adapter->TxSeq;
    Status = CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO, Frame, Padded);
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
    PUCHAR Frame = Adapter->TxBuffer;
    ULONG Total = SDPCM_HEADER_LEN + BCDC_HEADER_LEN + EthLen;
    ULONG Padded = ALIGN_UP(Total, ULONG);
    UCHAR Avail;
    NTSTATUS Status;

    Avail = (UCHAR)(Adapter->TxMax - Adapter->TxSeq);
    if ((Avail & 0x80) || Avail == 0 || Adapter->TxFlow)
    {
        return STATUS_DEVICE_BUSY;
    }

    if (Padded > CYW_CONTROL_BUFFER_SIZE)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(Frame, SDPCM_HEADER_LEN + BCDC_HEADER_LEN);
    Frame[0] = (UCHAR)(Total & 0xFF);
    Frame[1] = (UCHAR)((Total >> 8) & 0xFF);
    Frame[2] = (UCHAR)(~Frame[0]);
    Frame[3] = (UCHAR)(~Frame[1]);
    Frame[5] = SDPCM_CHANNEL_DATA;
    Frame[7] = SDPCM_HEADER_LEN;
    Frame[SDPCM_HEADER_LEN] = (UCHAR)(BCDC_PROTO_VER << 4);

    RtlCopyMemory(Frame + SDPCM_HEADER_LEN + BCDC_HEADER_LEN, Eth, EthLen);

    KeWaitForSingleObject(&Adapter->F2Lock, Executive, KernelMode, FALSE, NULL);
    Frame[SDPCM_SEQ_OFFSET] = Adapter->TxSeq;
    Status = CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO, Frame, Padded);
    if (NT_SUCCESS(Status))
    {
        Adapter->TxSeq = (UCHAR)(Adapter->TxSeq + 1);
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
                CywUpdateCredits(Adapter, Frame);
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
    ULONG BufLen = MsgLen + sizeof(CYW_BCDC_DCMD);
    ULONG RecvLen = 0;
    ULONG ReqId;
    NTSTATUS Status;

    Msg = CywAllocate(BufLen);
    if (Msg == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeWaitForSingleObject(&Adapter->CmdLock, Executive, KernelMode, FALSE, NULL);

    Adapter->BcdcRequestId++;
    ReqId = Adapter->BcdcRequestId;

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

    if (Adapter->RxThreadRunning)
    {
        LARGE_INTEGER Timeout;

        KeClearEvent(&Adapter->CtrlEvent);
        Status = CywSdpcmSendCtl(Adapter, Msg, MsgLen);
        if (NT_SUCCESS(Status))
        {
            Timeout.QuadPart = -20000000;
            Status = KeWaitForSingleObject(&Adapter->CtrlEvent, Executive,
                                           KernelMode, FALSE, &Timeout);
            if (Status == STATUS_SUCCESS)
            {
                RecvLen = Adapter->CtrlResponseLen;
                if (RecvLen > BufLen)
                {
                    RecvLen = BufLen;
                }
                RtlCopyMemory(Msg, Adapter->ControlBuffer, RecvLen);

                Dcmd = (PCYW_BCDC_DCMD)Msg;
                if (RecvLen >= sizeof(CYW_BCDC_DCMD) &&
                    ((Dcmd->Flags & BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT) != ReqId)
                {
                    DPRINT1("CYW: BCDC cmd %lu reqid mismatch\n", Cmd);
                    Status = STATUS_UNSUCCESSFUL;
                }
            }
            else
            {
                Status = STATUS_IO_TIMEOUT;
            }
        }
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
        BRCMF_E_DEAUTH_IND, BRCMF_E_ASSOC, BRCMF_E_REASSOC,
        BRCMF_E_REASSOC_IND, BRCMF_E_DISASSOC, BRCMF_E_DISASSOC_IND,
        BRCMF_E_LINK, BRCMF_E_MIC_ERROR, BRCMF_E_ROAM, BRCMF_E_IF,
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

NTSTATUS
CywConnect(
    _In_ PCYW_ADAPTER Adapter)
{
    CYW_SSID_LE Ssid;
    ULONG Value;
    ULONG Wsec;
    ULONG WpaAuth;
    NTSTATUS Status;

    switch (Adapter->UnicastCipher)
    {
        case DOT11_CIPHER_ALGO_CCMP: Wsec = CYW_WSEC_AES; break;
        case DOT11_CIPHER_ALGO_TKIP: Wsec = CYW_WSEC_TKIP; break;
        case DOT11_CIPHER_ALGO_WEP40:
        case DOT11_CIPHER_ALGO_WEP104: Wsec = CYW_WSEC_WEP; break;
        default: Wsec = CYW_WSEC_NONE; break;
    }

    switch (Adapter->AuthAlgorithm)
    {
        case DOT11_AUTH_ALGO_RSNA_PSK: WpaAuth = CYW_WPA2_AUTH_PSK; break;
        case DOT11_AUTH_ALGO_WPA_PSK: WpaAuth = CYW_WPA_AUTH_PSK; break;
        default: WpaAuth = CYW_WPA_AUTH_DISABLED; break;
    }

    Value = Wsec;
    CywFilCmdSet(Adapter, BRCMF_C_SET_WSEC, &Value, sizeof(Value));

    Value = 1;
    CywFilCmdSet(Adapter, BRCMF_C_SET_INFRA, &Value, sizeof(Value));

    Value = CYW_AUTH_OPEN;
    CywFilCmdSet(Adapter, BRCMF_C_SET_AUTH, &Value, sizeof(Value));

    Value = WpaAuth;
    CywFilCmdSet(Adapter, BRCMF_C_SET_WPA_AUTH, &Value, sizeof(Value));

    if (WpaAuth != CYW_WPA_AUTH_DISABLED)
    {
        UCHAR Ie[22];
        UCHAR Suite = (Wsec == CYW_WSEC_AES) ? 0x04 : 0x02;

        CywFilIovarSetInt(Adapter, "mfp", CYW_MFP_NONE);

        Ie[0] = 0x30; Ie[1] = 0x14;
        Ie[2] = 0x01; Ie[3] = 0x00;
        Ie[4] = 0x00; Ie[5] = 0x0F; Ie[6] = 0xAC; Ie[7] = Suite;
        Ie[8] = 0x01; Ie[9] = 0x00;
        Ie[10] = 0x00; Ie[11] = 0x0F; Ie[12] = 0xAC; Ie[13] = Suite;
        Ie[14] = 0x01; Ie[15] = 0x00;
        Ie[16] = 0x00; Ie[17] = 0x0F; Ie[18] = 0xAC; Ie[19] = 0x02;
        Ie[20] = 0x00; Ie[21] = 0x00;
        CywFilIovarSet(Adapter, "wpaie", Ie, sizeof(Ie));
    }

    CywFilIovarSetInt(Adapter, "roam_off", 1);

    RtlZeroMemory(&Ssid, sizeof(Ssid));
    Ssid.SsidLen = Adapter->DesiredSsidLength;
    RtlCopyMemory(Ssid.Ssid, Adapter->DesiredSsid, Adapter->DesiredSsidLength);
    Status = CywFilCmdSet(Adapter, BRCMF_C_SET_SSID, &Ssid, sizeof(Ssid));

    DPRINT1("CYW: connect '%.*s' wsec %lu wpa_auth 0x%lx status 0x%08lx\n",
            (int)Adapter->DesiredSsidLength, Adapter->DesiredSsid,
            Wsec, WpaAuth, Status);
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
    NdisReleaseSpinLock(&Adapter->Lock);

    return CywFilCmdSet(Adapter, BRCMF_C_DISASSOC, &Value, sizeof(Value));
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
    Entry->Rssi = (LONG)(SHORT)(Bss[76] | (Bss[77] << 8));
    Entry->LinkQuality = (Entry->Rssi >= -50) ? 100 :
                         (Entry->Rssi <= -100) ? 0 : (2 * (Entry->Rssi + 100));
    Entry->ChannelNumber = (Bss[71] | (Bss[72] << 8)) & 0xFF;
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
    else if (EventType == BRCMF_E_LINK)
    {
        USHORT LinkFlags = RtlUshortByteSwap(Msg->Flags);
        if (LinkFlags & BRCMF_EVENT_MSG_LINK)
        {
            NdisAcquireSpinLock(&Adapter->Lock);
            RtlCopyMemory(Adapter->ConnectedBssid, Msg->Addr, CYW_ADDRESS_LENGTH);
            Adapter->Associated = TRUE;
            NdisReleaseSpinLock(&Adapter->Lock);
            DPRINT1("CYW: LINK up, associated - indicating ASSOCIATION_COMPLETION\n");
            CywIndicateAssocComplete(Adapter, DOT11_ASSOC_STATUS_SUCCESS);
        }
        else
        {
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->Associated = FALSE;
            Adapter->LinkUp = FALSE;
            NdisReleaseSpinLock(&Adapter->Lock);
            DPRINT1("CYW: LINK down\n");
        }
    }
    else if (EventType == BRCMF_E_DEAUTH || EventType == BRCMF_E_DEAUTH_IND ||
             EventType == BRCMF_E_DISASSOC || EventType == BRCMF_E_DISASSOC_IND)
    {
        DPRINT1("CYW: DEAUTH/DISASSOC evt %lu status %lu reason %lu\n",
                EventType, EventStatus, RtlUlongByteSwap(Msg->Reason));
    }
    else if (EventType == BRCMF_E_REASSOC || EventType == BRCMF_E_REASSOC_IND ||
             EventType == BRCMF_E_ROAM || EventType == BRCMF_E_MIC_ERROR)
    {
        DPRINT1("CYW: REASSOC/ROAM/MIC evt %lu status %lu reason %lu\n",
                EventType, EventStatus, RtlUlongByteSwap(Msg->Reason));
    }
    else if (EventType == BRCMF_E_SET_SSID)
    {
        DPRINT1("CYW: SET_SSID event status %lu\n", EventStatus);
        if (EventStatus != BRCMF_E_STATUS_SUCCESS)
        {
            CywIndicateAssocComplete(Adapter, DOT11_ASSOC_STATUS_FAILURE);
        }
    }
}

PNET_BUFFER_LIST
CywRxData(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Body,
    _In_ ULONG BodyLen)
{
    PUCHAR Pay;
    ULONG Off;
    ULONG PayLen;
    ULONG FrameLen;
    PUCHAR Frame;
    PCYW_DOT11_HEADER Dot11;
    PCYW_SNAP_HEADER Snap;
    PMDL Mdl;
    PNET_BUFFER_LIST Nbl;

    if (BodyLen < BCDC_HEADER_LEN)
    {
        return NULL;
    }

    Off = BCDC_HEADER_LEN + ((ULONG)Body[3] << 2);
    if (Off + sizeof(CYW_ETHER_HEADER) > BodyLen)
    {
        return NULL;
    }

    Pay = Body + Off;
    PayLen = BodyLen - Off;

    FrameLen = sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER) +
               (PayLen - sizeof(CYW_ETHER_HEADER));
    if (FrameLen > CYW_MAX_FRAME_SIZE)
    {
        return NULL;
    }

    Frame = CywAllocate(FrameLen);
    if (Frame == NULL)
    {
        return NULL;
    }

    Dot11 = (PCYW_DOT11_HEADER)Frame;
    RtlZeroMemory(Dot11, sizeof(CYW_DOT11_HEADER));
    Dot11->FrameControl[0] = CYW_FC0_TYPE_DATA;
    Dot11->FrameControl[1] = CYW_FC1_FROMDS;
    RtlCopyMemory(Dot11->Address1, Adapter->CurrentAddress, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(Dot11->Address2, Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(Dot11->Address3, Pay + CYW_ADDRESS_LENGTH, CYW_ADDRESS_LENGTH);

    Snap = (PCYW_SNAP_HEADER)(Frame + sizeof(CYW_DOT11_HEADER));
    Snap->Dsap = CYW_SNAP_DSAP;
    Snap->Ssap = CYW_SNAP_SSAP;
    Snap->Control = CYW_SNAP_CONTROL;
    Snap->Oui[0] = 0;
    Snap->Oui[1] = 0;
    Snap->Oui[2] = 0;
    Snap->EtherType[0] = Pay[12];
    Snap->EtherType[1] = Pay[13];

    RtlCopyMemory(Frame + sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER),
                  Pay + sizeof(CYW_ETHER_HEADER),
                  PayLen - sizeof(CYW_ETHER_HEADER));

    Mdl = NdisAllocateMdl(Adapter->MiniportAdapterHandle, Frame, FrameLen);
    if (Mdl == NULL)
    {
        CywFree(Frame);
        return NULL;
    }
    MmBuildMdlForNonPagedPool(Mdl);

    Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0, Mdl, 0, FrameLen);
    if (Nbl == NULL)
    {
        NdisFreeMdl(Mdl);
        CywFree(Frame);
        return NULL;
    }

    Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
    NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

    return Nbl;
}

static
VOID
NTAPI
CywRxThread(
    _In_ PVOID Context)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)Context;
    PUCHAR Frame = Adapter->RxBuffer;
    NTSTATUS Status;
    ULONG FrameLen;
    UCHAR Channel;
    ULONG DataOffset;
    PNET_BUFFER_LIST ChainHead = NULL, ChainTail = NULL;
    ULONG ChainCount = 0;

    while (Adapter->RxThreadStop == 0)
    {
        KeWaitForSingleObject(&Adapter->F2Lock, Executive, KernelMode, FALSE, NULL);

        Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                  Frame, SDPCM_HEADER_LEN);
        FrameLen = Frame[0] | (Frame[1] << 8);
        if (!NT_SUCCESS(Status) || ((Frame[0] ^ Frame[2]) != 0xFF) ||
            FrameLen < SDPCM_HEADER_LEN || FrameLen > CYW_CONTROL_BUFFER_SIZE)
        {
            LARGE_INTEGER RxWait;
            KeReleaseMutex(&Adapter->F2Lock, FALSE);
            if (ChainHead != NULL)
            {
                NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                                   ChainHead, NDIS_DEFAULT_PORT_NUMBER,
                                                   ChainCount, 0);
                ChainHead = NULL;
                ChainTail = NULL;
                ChainCount = 0;
            }
            RxWait.QuadPart = -2500;
            KeDelayExecutionThread(KernelMode, FALSE, &RxWait);
            continue;
        }

        CywUpdateCredits(Adapter, Frame);

        if (FrameLen > SDPCM_HEADER_LEN)
        {
            ULONG Body = FrameLen - SDPCM_HEADER_LEN;
            ULONG Blocks = Body / CYW_F2_BLOCKSIZE;
            ULONG Done = 0;
            if (Blocks > 0)
            {
                CywSdioReadBlocks(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                  Frame + SDPCM_HEADER_LEN, Blocks * CYW_F2_BLOCKSIZE,
                                  CYW_F2_BLOCKSIZE);
                Done = Blocks * CYW_F2_BLOCKSIZE;
            }
            if (Done < Body)
            {
                CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_RADIO, SDIO_F2_FIFO,
                                 Frame + SDPCM_HEADER_LEN + Done,
                                 ALIGN_UP(Body - Done, ULONG));
            }
        }

        KeReleaseMutex(&Adapter->F2Lock, FALSE);

        Channel = Frame[SDPCM_CHANNEL_OFFSET] & 0x0F;
        DataOffset = Frame[SDPCM_DOFFSET_OFFSET];
        if (DataOffset >= FrameLen)
        {
            continue;
        }

        if (Channel == SDPCM_CHANNEL_CONTROL)
        {
            ULONG Payload = FrameLen - DataOffset;
            RtlCopyMemory(Adapter->ControlBuffer, Frame + DataOffset, Payload);
            Adapter->CtrlResponseLen = Payload;
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
            if (Nbl != NULL)
            {
                if (ChainTail != NULL)
                {
                    NET_BUFFER_LIST_NEXT_NBL(ChainTail) = Nbl;
                }
                else
                {
                    ChainHead = Nbl;
                }
                ChainTail = Nbl;
                ChainCount++;
            }
            if (ChainCount >= 32)
            {
                NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                                   ChainHead, NDIS_DEFAULT_PORT_NUMBER,
                                                   ChainCount, 0);
                ChainHead = NULL;
                ChainTail = NULL;
                ChainCount = 0;
            }
        }
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
CywStartRxThread(
    _In_ PCYW_ADAPTER Adapter)
{
    HANDLE Handle;
    NTSTATUS Status;

    Adapter->RxThreadStop = 0;
    Adapter->RxThread = NULL;

    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  CywRxThread, Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: RxThread create failed 0x%08lx\n", Status);
        return Status;
    }

    Status = ObReferenceObjectByHandle(Handle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, &Adapter->RxThread, NULL);
    ZwClose(Handle);
    if (NT_SUCCESS(Status))
    {
        Adapter->RxThreadRunning = TRUE;
    }
    else
    {
        InterlockedExchange(&Adapter->RxThreadStop, 1);
        Adapter->RxThread = NULL;
    }
    return Status;
}

VOID
CywStopRxThread(
    _In_ PCYW_ADAPTER Adapter)
{
    if (!Adapter->RxThreadRunning)
    {
        return;
    }

    InterlockedExchange(&Adapter->RxThreadStop, 1);

    if (Adapter->RxThread != NULL)
    {
        KeWaitForSingleObject(Adapter->RxThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Adapter->RxThread);
        Adapter->RxThread = NULL;
    }

    Adapter->RxThreadRunning = FALSE;
}
