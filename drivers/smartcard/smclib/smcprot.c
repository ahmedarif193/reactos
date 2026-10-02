/*
 * PROJECT:     LiberNT Smart Card Driver Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Answer to reset parsing and the T=0, T=1 and raw protocols
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "smcpriv.h"

#define NDEBUG
#include <debug.h>

CLOCK_RATE_CONVERSION SmcClockRateConversion[16] =
{
    { 372,  4000000 },
    { 372,  5000000 },
    { 558,  6000000 },
    { 744,  8000000 },
    { 1116, 12000000 },
    { 1488, 16000000 },
    { 1860, 20000000 },
    { 0,    0 },
    { 0,    0 },
    { 512,  5000000 },
    { 768,  7500000 },
    { 1024, 10000000 },
    { 1536, 15000000 },
    { 2048, 20000000 },
    { 0,    0 },
    { 0,    0 }
};

BIT_RATE_ADJUSTMENT SmcBitRateAdjustment[16] =
{
    { 0,  0 },
    { 1,  1 },
    { 2,  1 },
    { 4,  1 },
    { 8,  1 },
    { 16, 1 },
    { 32, 1 },
    { 64, 1 },
    { 12, 1 },
    { 20, 1 },
    { 0,  0 },
    { 0,  0 },
    { 0,  0 },
    { 0,  0 },
    { 0,  0 },
    { 0,  0 }
};

VOID
NTAPI
SmartcardInitializeCardCapabilities(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSCARD_CARD_CAPABILITIES Card = &SmartcardExtension->CardCapabilities;
    ULONG MaxIfsd = SmartcardExtension->ReaderCapabilities.MaxIFSD;

    RtlZeroMemory(Card, sizeof(*Card));
    Card->ClockRateConversion = SmcClockRateConversion;
    Card->BitRateAdjustment = SmcBitRateAdjustment;
    Card->Protocol.Supported = SCARD_PROTOCOL_RAW;

    SmartcardExtension->T1.State = T1_INIT;
    SmartcardExtension->T1.IFSC = 0;
    SmartcardExtension->T1.NAD = 0;
    SmartcardExtension->T1.SSN = 0;
    SmartcardExtension->T1.RSN = 0;
    SmartcardExtension->T1.IFSD = (MaxIfsd == 0 || MaxIfsd > T1_IFSD) ? T1_IFSD_DEFAULT : (UCHAR)MaxIfsd;
}

VOID
NTAPI
SmartcardInvertData(
    PUCHAR Buffer,
    ULONG Length)
{
    ULONG Index, Bit;

    for (Index = 0; Index < Length; Index++)
    {
        UCHAR Value = Buffer[Index];
        UCHAR Result = 0;

        for (Bit = 0; Bit < 8; Bit++)
        {
            Result = (UCHAR)((Result << 1) | (Value & 1));
            Value >>= 1;
        }

        Buffer[Index] = (UCHAR)~Result;
    }
}

NTSTATUS
NTAPI
SmartcardUpdateCardCapabilities(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSCARD_CARD_CAPABILITIES Card = &SmartcardExtension->CardCapabilities;
    PSCARD_READER_CAPABILITIES Reader = &SmartcardExtension->ReaderCapabilities;
    PUCHAR Atr = Card->ATR.Buffer;
    ULONG Length = Card->ATR.Length;
    ULONG Index, Level, Protocol, Supported, Historical, Protocols, Seen;
    UCHAR Y, Check, Fl, Dl, II, P, N, Wi, Ifsc, Cwi, Bwi, Edc, SpecificMode;
    BOOLEAN Specific, NeedCheck, Negotiable;
    ULONG Etu, Clock;
    KIRQL Irql;

    if (Length < 2)
        return STATUS_UNRECOGNIZED_MEDIA;

    if (Atr[0] == 0x03)
    {
        SmartcardInvertData(Atr, Length);
        Card->InversConvention = TRUE;
    }

    if (Atr[0] != 0x3B && Atr[0] != 0x3F)
        return STATUS_UNRECOGNIZED_MEDIA;

    Card->Protocol.Supported = 0;

    Fl = 1;
    Dl = 1;
    II = 0;
    P = 50;
    N = 0;
    Wi = 10;
    Ifsc = T1_IFSD_DEFAULT;
    Cwi = T1_CWI_DEFAULT;
    Bwi = T1_BWI_DEFAULT;
    Edc = 0;
    Specific = FALSE;
    SpecificMode = 0;
    NeedCheck = FALSE;
    Negotiable = FALSE;

    Y = Atr[1] >> 4;
    Historical = Atr[1] & 0x0F;
    Index = 2;
    Level = 1;
    Protocol = 0;
    Protocols = 0;
    Seen = 0;
    Supported = 0;

    for (;;)
    {
        if (Y & 1)
        {
            if (Index >= Length)
                return STATUS_UNRECOGNIZED_MEDIA;

            if (Level == 1)
            {
                Fl = Atr[Index] >> 4;
                Dl = Atr[Index] & 0x0F;
                if (Atr[Index] != 0x11)
                    Negotiable = TRUE;
            }
            else
            {
                if (Level == 2)
                {
                    Specific = TRUE;
                    SpecificMode = Atr[Index];
                }

                if (Protocol == 1 && Atr[Index] != 0)
                    Ifsc = Atr[Index];
            }
            Index++;
        }

        if (Y & 2)
        {
            if (Index >= Length)
                return STATUS_UNRECOGNIZED_MEDIA;

            if (Level == 1)
            {
                II = Atr[Index] >> 6;
                P = (UCHAR)((Atr[Index] & 0x1F) * 10);
            }
            else
            {
                if (Level == 2)
                    P = Atr[Index];

                if (Protocol == 1)
                {
                    if ((Atr[Index] >> 4) != 0)
                        Bwi = Atr[Index] >> 4;
                    if ((Atr[Index] & 0x0F) != 0)
                        Cwi = Atr[Index] & 0x0F;
                }
            }
            Index++;
        }

        if (Y & 4)
        {
            if (Index >= Length)
                return STATUS_UNRECOGNIZED_MEDIA;

            if (Level == 1)
                N = Atr[Index];
            else
            {
                if (Level == 2)
                    Wi = Atr[Index];

                if (Protocol == 1)
                    Edc = Atr[Index] & T1_CRC_CHECK;
            }
            Index++;
        }

        if (!(Y & 8))
            break;

        if (Index >= Length)
            return STATUS_UNRECOGNIZED_MEDIA;

        Protocol = Atr[Index] & 0x0F;
        Y = Atr[Index] >> 4;
        if (!(Seen & (1 << Protocol)))
        {
            Seen |= 1 << Protocol;
            Protocols++;
        }

        if (Protocol == 0 && Level == 1)
            Supported |= SCARD_PROTOCOL_T0;
        else if (Protocol == 1)
            Supported |= SCARD_PROTOCOL_T1;

        if (Protocol != 0)
            NeedCheck = TRUE;

        Index++;
        Level++;
    }

    if (Protocols == 0)
        Supported = SCARD_PROTOCOL_T0;

    Card->HistoricalChars.Length = (UCHAR)Historical;
    if (Index + Historical > Length)
        return STATUS_UNRECOGNIZED_MEDIA;

    RtlCopyMemory(Card->HistoricalChars.Buffer, &Atr[Index], Historical);
    Index += Historical;

    if (NeedCheck)
    {
        if (Index >= Length)
            return STATUS_UNRECOGNIZED_MEDIA;

        Check = 0;
        for (Level = 1; Level <= Index; Level++)
            Check ^= Atr[Level];
        if (Check != 0)
            return STATUS_UNRECOGNIZED_MEDIA;
        Index++;
    }

    if (Index != Length)
        return STATUS_UNRECOGNIZED_MEDIA;

    Card->Fl = Fl;
    Card->Dl = Dl;
    Card->II = II;
    Card->P = P;
    Card->N = N;

    Clock = Reader->CLKFrequency.Default;
    if (SmcClockRateConversion[Fl].F == 0 || SmcBitRateAdjustment[Dl].DNumerator == 0 || Clock == 0)
        return STATUS_UNRECOGNIZED_MEDIA;

    Card->PtsData.Type = PTS_TYPE_DEFAULT;
    Card->PtsData.Fl = 1;
    Card->PtsData.Dl = 1;
    Card->PtsData.CLKFrequency = Clock;
    Card->PtsData.DataRate = Reader->DataRate.Default;
    Card->PtsData.StopBits = N == 255 ? 1 : 2;

    Etu = 1 + (SmcClockRateConversion[1].F * 1000) / Clock;
    Card->etu = Etu;
    Card->GT = N == 255 ? 0 : N * Etu;

    if (Supported & SCARD_PROTOCOL_T0)
    {
        Card->T0.WI = Wi;
        Card->T0.WT = 1 + Wi * 960 * Etu;
    }

    if (Supported & SCARD_PROTOCOL_T1)
    {
        Card->T1.IFSC = Ifsc;
        Card->T1.CWI = Cwi;
        Card->T1.BWI = Bwi;
        Card->T1.EDC = Edc;
        Card->T1.CWT = 1 + ((1 << Cwi) + 11) * Etu;
        Card->T1.BWT = 1 + ((((ULONG)1 << Bwi) * 960 * 372) / Clock + 11 * Etu) * 1000;
    }

    Card->Protocol.Supported = Supported | SCARD_PROTOCOL_RAW;

    KeAcquireSpinLock(&SmartcardExtension->OsData->SpinLock, &Irql);
    if (Specific)
    {
        Card->Protocol.Selected = 1 << (SpecificMode & 0x0F);
        Reader->CurrentState = SCARD_SPECIFIC;
    }
    else if (Protocols > 1 || Negotiable)
    {
        Card->Protocol.Selected = SCARD_PROTOCOL_UNDEFINED;
        Reader->CurrentState = SCARD_NEGOTIABLE;
    }
    else
    {
        Card->Protocol.Selected = Supported;
        Reader->CurrentState = SCARD_SPECIFIC;
    }
    KeReleaseSpinLock(&SmartcardExtension->OsData->SpinLock, Irql);

    return STATUS_SUCCESS;
}

BOOLEAN
SmcIsNegotiable(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _Out_ PULONG Selected)
{
    PUCHAR Atr = SmartcardExtension->CardCapabilities.ATR.Buffer;
    ULONG Length = SmartcardExtension->CardCapabilities.ATR.Length;
    ULONG Index = 2, Level = 1, Seen = 0, Protocols = 0, Protocol;
    BOOLEAN Negotiable = FALSE;
    UCHAR Y;

    *Selected = SmartcardExtension->CardCapabilities.Protocol.Supported & SCARD_PROTOCOL_Tx;
    if (Length < 2)
        return FALSE;

    Y = Atr[1] >> 4;
    for (;;)
    {
        if (Y & 1)
        {
            if (Index >= Length)
                break;
            if (Level == 1 && Atr[Index] != 0x11)
                Negotiable = TRUE;
            if (Level == 2)
            {
                *Selected = 1 << (Atr[Index] & 0x0F);
                return FALSE;
            }
            Index++;
        }

        if (Y & 2)
            Index++;
        if (Y & 4)
            Index++;
        if (!(Y & 8) || Index >= Length)
            break;

        Protocol = Atr[Index] & 0x0F;
        Y = Atr[Index] >> 4;
        if (!(Seen & (1 << Protocol)))
        {
            Seen |= 1 << Protocol;
            Protocols++;
        }
        Index++;
        Level++;
    }

    return Negotiable || Protocols > 1;
}

static
ULONG
SmcReaderClock(
    _In_ PSCARD_READER_CAPABILITIES Reader,
    _In_ ULONG Limit)
{
    ULONG Best = 0;
    ULONG Index;

    if (Reader->CLKFrequenciesSupported.Entries != 0 && Reader->CLKFrequenciesSupported.List != NULL)
    {
        for (Index = 0; Index < Reader->CLKFrequenciesSupported.Entries; Index++)
        {
            ULONG Clock = Reader->CLKFrequenciesSupported.List[Index];

            if (Clock <= Limit && Clock > Best)
                Best = Clock;
        }
        return Best;
    }

    if (Reader->CLKFrequency.Default <= Limit)
        Best = Reader->CLKFrequency.Default;
    if (Reader->CLKFrequency.Max <= Limit && Reader->CLKFrequency.Max > Best)
        Best = Reader->CLKFrequency.Max;
    return Best;
}

static
ULONG
SmcReaderDataRate(
    _In_ PSCARD_READER_CAPABILITIES Reader,
    _In_ ULONG Rate)
{
    ULONG Rates[2];
    PULONG List = Rates;
    ULONG Count = 2;
    ULONG Index;

    Rates[0] = Reader->DataRate.Default;
    Rates[1] = Reader->DataRate.Max;
    if (Reader->DataRatesSupported.Entries != 0 && Reader->DataRatesSupported.List != NULL)
    {
        List = Reader->DataRatesSupported.List;
        Count = Reader->DataRatesSupported.Entries;
    }

    for (Index = 0; Index < Count; Index++)
    {
        ULONG Difference = Rate > List[Index] ? Rate - List[Index] : List[Index] - Rate;

        if (List[Index] != 0 && (ULONGLONG)Difference * 100 < List[Index])
            return List[Index];
    }

    return 0;
}

VOID
SmcSelectTransmission(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_ BOOLEAN Optimal)
{
    PSCARD_READER_CAPABILITIES Reader = &SmartcardExtension->ReaderCapabilities;
    PSCARD_CARD_CAPABILITIES Card = &SmartcardExtension->CardCapabilities;
    PPTS_DATA Pts = &Card->PtsData;
    ULONG CardF = SmcClockRateConversion[Card->Fl & 15].F;
    ULONG Fl, Dl;

    Pts->Type = Optimal ? PTS_TYPE_OPTIMAL : PTS_TYPE_DEFAULT;
    Pts->Fl = 1;
    Pts->Dl = 1;
    Pts->CLKFrequency = Reader->CLKFrequency.Default;
    Pts->DataRate = Reader->DataRate.Default;
    if (!Optimal)
        return;

    for (Dl = Card->Dl & 15; Dl >= 1; Dl--)
    {
        ULONG D = SmcBitRateAdjustment[Dl].DNumerator;

        if (D == 0)
            continue;

        for (Fl = 15; Fl >= 1; Fl--)
        {
            ULONG F = SmcClockRateConversion[Fl].F;
            ULONG Clock, Rate;

            if (F == 0 || F > CardF)
                continue;

            Clock = SmcReaderClock(Reader, SmcClockRateConversion[Fl].fs / 1000);
            if (Clock == 0)
                continue;

            Rate = SmcReaderDataRate(Reader, (ULONG)(((ULONGLONG)Clock * 1000 * D) / F));
            if (Rate != 0)
            {
                Pts->Fl = (UCHAR)Fl;
                Pts->Dl = (UCHAR)Dl;
                Pts->CLKFrequency = Clock;
                Pts->DataRate = Rate;
                return;
            }
        }
    }
}

static
NTSTATUS
SmcCopyRequest(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_ ULONG Skip,
    _In_ NTSTATUS Overflow,
    _Out_ PUCHAR *Data,
    _Out_ PULONG Length)
{
    PSMARTCARD_REQUEST Request = &SmartcardExtension->SmartcardRequest;
    ULONG Total = SmartcardExtension->IoRequest.RequestBufferLength;

    if (Total < sizeof(SCARD_IO_REQUEST))
        return STATUS_INVALID_PARAMETER;

    if (Request->BufferLength + Total >= Request->BufferSize)
        return Overflow;

    *Data = Request->Buffer + Request->BufferLength;
    *Length = Total - Skip;
    RtlCopyMemory(*Data, SmartcardExtension->IoRequest.RequestBuffer + Skip, *Length);
    Request->BufferLength += *Length;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SmartcardRawRequest(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    PUCHAR Data;
    ULONG Length;

    return SmcCopyRequest(SmartcardExtension, 0, STATUS_BUFFER_TOO_SMALL, &Data, &Length);
}

NTSTATUS
NTAPI
SmartcardRawReply(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSMARTCARD_REPLY Reply = &SmartcardExtension->SmartcardReply;

    if (SmartcardExtension->IoRequest.ReplyBufferLength < Reply->BufferLength)
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(SmartcardExtension->IoRequest.ReplyBuffer, Reply->Buffer, Reply->BufferLength);
    *SmartcardExtension->IoRequest.Information = Reply->BufferLength;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SmartcardT0Request(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSMARTCARD_REQUEST Request = &SmartcardExtension->SmartcardRequest;
    PUCHAR Data;
    ULONG Length;
    NTSTATUS Status;

    Status = SmcCopyRequest(SmartcardExtension, sizeof(SCARD_IO_REQUEST), STATUS_BUFFER_OVERFLOW, &Data, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length < 4)
        return STATUS_INVALID_PARAMETER;

    if (Length == 4)
    {
        Data[4] = 0;
        Request->BufferLength++;
        SmartcardExtension->T0.Lc = 0;
        SmartcardExtension->T0.Le = 0;
        return STATUS_SUCCESS;
    }

    if (Length == 5)
    {
        SmartcardExtension->T0.Lc = 0;
        SmartcardExtension->T0.Le = Data[4] != 0 ? Data[4] : 256;
        return STATUS_SUCCESS;
    }

    SmartcardExtension->T0.Lc = Data[4];
    SmartcardExtension->T0.Le = 0;
    if (Length != 5 + SmartcardExtension->T0.Lc)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

static
NTSTATUS
SmcCompleteReply(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_ ULONG Protocol,
    _In_ ULONG Length)
{
    PSCARD_IO_REQUEST Header = (PSCARD_IO_REQUEST)SmartcardExtension->IoRequest.ReplyBuffer;

    Header->dwProtocol = Protocol;
    Header->cbPciLength = sizeof(SCARD_IO_REQUEST);
    *SmartcardExtension->IoRequest.Information = sizeof(SCARD_IO_REQUEST) + Length;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SmartcardT0Reply(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSMARTCARD_REPLY Reply = &SmartcardExtension->SmartcardReply;

    if (Reply->BufferLength < 2)
        return STATUS_DEVICE_PROTOCOL_ERROR;

    if (SmartcardExtension->IoRequest.ReplyBufferLength < sizeof(SCARD_IO_REQUEST) + Reply->BufferLength)
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(SmartcardExtension->IoRequest.ReplyBuffer + sizeof(SCARD_IO_REQUEST),
                  Reply->Buffer,
                  Reply->BufferLength);
    return SmcCompleteReply(SmartcardExtension, SCARD_PROTOCOL_T0, Reply->BufferLength);
}

static
USHORT
SmcCrc(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    USHORT Crc = 0;
    ULONG Index, Bit;

    for (Index = 0; Index < Length; Index++)
    {
        Crc ^= Data[Index];
        for (Bit = 0; Bit < 8; Bit++)
            Crc = (Crc & 1) ? (USHORT)((Crc >> 1) ^ 0xA001) : (USHORT)(Crc >> 1);
    }

    return Crc;
}

static
ULONG
SmcEdcLength(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension)
{
    return (SmartcardExtension->CardCapabilities.T1.EDC & T1_CRC_CHECK) ?
           SCARD_T1_EPILOGUE_LENGTH : SCARD_T1_EPILOGUE_LENGTH_LRC;
}

static
VOID
SmcEdc(
    _In_ PSMARTCARD_EXTENSION SmartcardExtension,
    _In_reads_bytes_(Length) const UCHAR *Block,
    _In_ ULONG Length,
    _Out_writes_bytes_(2) PUCHAR Edc)
{
    if (SmartcardExtension->CardCapabilities.T1.EDC & T1_CRC_CHECK)
    {
        USHORT Crc = SmcCrc(Block, Length);

        Edc[0] = (UCHAR)(Crc >> 8);
        Edc[1] = (UCHAR)Crc;
    }
    else
    {
        UCHAR Lrc = 0;
        ULONG Index;

        for (Index = 0; Index < Length; Index++)
            Lrc ^= Block[Index];
        Edc[0] = Lrc;
        Edc[1] = 0;
    }
}

NTSTATUS
NTAPI
SmartcardT1Request(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSMARTCARD_REQUEST Request = &SmartcardExtension->SmartcardRequest;
    PT1_DATA T1 = &SmartcardExtension->T1;
    ULONG EdcLength = SmcEdcLength(SmartcardExtension);
    ULONG Total = SmartcardExtension->IoRequest.RequestBufferLength;
    ULONG InfLength = 0;
    PUCHAR Block;
    UCHAR Edc[2];
    UCHAR Pcb;

    if (T1->State == T1_INIT || T1->State == T1_START || T1->State == T1_RESTART)
    {
        if (Total < sizeof(SCARD_IO_REQUEST))
            return STATUS_INVALID_PARAMETER;

        T1->BytesToSend = Total - sizeof(SCARD_IO_REQUEST);
        T1->BytesSent = 0;
        T1->BytesReceived = 0;
        T1->MoreData = FALSE;
        T1->Resend = 0;
        T1->OriginalState = 0;

        if (T1->State == T1_INIT)
        {
            T1->IFSC = SmartcardExtension->CardCapabilities.T1.IFSC;
            T1->SSN = 0;
            T1->RSN = 0;
            T1->Resynch = 0;
            T1->State = T1_IFS_REQUEST;
        }
        else
        {
            if (T1->State == T1_RESTART)
            {
                T1->SSN = 0;
                T1->RSN = 0;
            }
            else
            {
                T1->Resynch = 0;
            }

            T1->IFSC = SmartcardExtension->CardCapabilities.T1.IFSC;
            T1->State = T1_I_BLOCK;
        }
    }

    if (T1->State == T1_I_BLOCK)
        InfLength = min(T1->BytesToSend, T1->IFSC);
    else if (T1->State == T1_IFS_REQUEST || T1->State == T1_IFS_RESPONSE || T1->State == T1_WTX_RESPONSE)
        InfLength = 1;

    if (Request->BufferLength > Request->BufferSize ||
        Request->BufferSize - Request->BufferLength < SCARD_T1_PROLOGUE_LENGTH + InfLength + EdcLength)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    Block = Request->Buffer + Request->BufferLength;

    switch (T1->State)
    {
        case T1_IFS_REQUEST:
            Pcb = T1_IFS_REQUEST;
            Block[3] = T1->IFSD;
            break;

        case T1_I_BLOCK:
            T1->MoreData = T1->BytesToSend > T1->IFSC;
            Pcb = (UCHAR)((T1->SSN << 6) | (T1->MoreData ? T1_MORE_DATA : 0));
            RtlCopyMemory(&Block[3],
                          SmartcardExtension->IoRequest.RequestBuffer + sizeof(SCARD_IO_REQUEST) + T1->BytesSent,
                          InfLength);
            T1->InfBytesSent = T1->IFSC;
            break;

        case T1_R_BLOCK:
            Pcb = (UCHAR)(0x80 | (T1->RSN << 4) | T1->LastError);
            if (T1->LastError != 0)
            {
                T1->LastError = 0;
                T1->State = T1->OriginalState;
            }
            break;

        case T1_RESYNCH_REQUEST:
            Pcb = T1_RESYNCH_REQUEST;
            break;

        case T1_IFS_RESPONSE:
            Pcb = T1_IFS_RESPONSE;
            Block[3] = T1->IFSC;
            T1->State = T1->OriginalState;
            break;

        case T1_WTX_RESPONSE:
            Pcb = T1_WTX_RESPONSE;
            Block[3] = T1->Wtx;
            T1->State = T1->OriginalState;
            T1->OriginalState = 0;
            break;

        case T1_ABORT_RESPONSE:
            Pcb = T1_ABORT_RESPONSE;
            T1->State = T1_START;
            break;

        default:
            return STATUS_INVALID_DEVICE_STATE;
    }

    Block[0] = T1->NAD;
    Block[1] = Pcb;
    Block[2] = (UCHAR)InfLength;
    SmcEdc(SmartcardExtension, Block, SCARD_T1_PROLOGUE_LENGTH + InfLength, Edc);
    RtlCopyMemory(&Block[SCARD_T1_PROLOGUE_LENGTH + InfLength], Edc, EdcLength);
    Request->BufferLength += SCARD_T1_PROLOGUE_LENGTH + InfLength + EdcLength;
    T1->WaitForReply = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SmcT1Fatal(
    _In_ PT1_DATA T1)
{
    T1->State = T1_START;
    return STATUS_DEVICE_PROTOCOL_ERROR;
}

static
NTSTATUS
SmcT1Resynch(
    _In_ PT1_DATA T1)
{
    T1->Resend = 0;
    T1->Resynch++;
    T1->OriginalState = T1->State;
    T1->State = T1_RESYNCH_REQUEST;
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
SmcT1Error(
    _In_ PT1_DATA T1,
    _In_ UCHAR Error)
{
    T1->LastError = Error;
    T1->Resend++;

    if (T1->State == T1_RESYNCH_REQUEST)
    {
        T1->Resynch++;
        if (T1->Resynch > T1_MAX_RETRIES)
        {
            T1->State = T1->OriginalState;
            T1->OriginalState = 0;
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (T1->Resend > T1_MAX_RETRIES)
        return SmcT1Resynch(T1);

    T1->OriginalState = T1->State;
    T1->State = T1_R_BLOCK;
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
SmartcardT1Reply(
    PSMARTCARD_EXTENSION SmartcardExtension)
{
    PSMARTCARD_REPLY Reply = &SmartcardExtension->SmartcardReply;
    PT1_DATA T1 = &SmartcardExtension->T1;
    PUCHAR Block = Reply->Buffer;
    ULONG EdcLength = SmcEdcLength(SmartcardExtension);
    ULONG Length = Reply->BufferLength;
    UCHAR Edc[2];
    UCHAR Pcb, InfLength;

    if (T1->Resynch > T1_MAX_RETRIES)
        return STATUS_INTERNAL_ERROR;

    T1->WaitForReply = FALSE;
    T1->Wtx = 0;

    if (Length < SCARD_T1_PROLOGUE_LENGTH + EdcLength ||
        Length != SCARD_T1_PROLOGUE_LENGTH + Block[2] + EdcLength)
    {
        return SmcT1Error(T1, T1_ERROR_OTHER);
    }

    SmcEdc(SmartcardExtension, Block, Length - EdcLength, Edc);
    if (RtlCompareMemory(Edc, &Block[Length - EdcLength], EdcLength) != EdcLength)
        return SmcT1Error(T1, T1_ERROR_CHKSUM);

    Pcb = Block[1];
    InfLength = Block[2];
    T1->LastError = 0;

    if (T1->State == T1_RESYNCH_REQUEST)
    {
        if (Pcb != T1_RESYNCH_RESPONSE)
        {
            T1->Resynch++;
            return SmcT1Fatal(T1);
        }

        T1->LastError = 0;
        T1->Resend = 0;
        T1->State = T1_RESTART;
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (!(Pcb & 0x80))
    {
        if (T1->State == T1_IFS_REQUEST)
            return SmcT1Fatal(T1);

        if (((Pcb >> 6) & 1) != T1->RSN)
            return SmcT1Error(T1, T1_ERROR_OTHER);

        if (T1->State == T1_I_BLOCK)
            T1->SSN ^= 1;
        T1->RSN ^= 1;

        if (T1->BytesReceived + InfLength > Reply->BufferSize)
        {
            T1->State = T1_START;
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlCopyMemory(T1->ReplyData + T1->BytesReceived, &Block[3], InfLength);
        T1->BytesReceived += InfLength;
        T1->Resend = 0;
        T1->OriginalState = 0;

        if (Pcb & T1_MORE_DATA)
        {
            T1->State = T1_R_BLOCK;
            return STATUS_MORE_PROCESSING_REQUIRED;
        }

        T1->State = T1_START;
        if (SmartcardExtension->IoRequest.ReplyBufferLength < sizeof(SCARD_IO_REQUEST) + T1->BytesReceived)
        {
            T1->BytesReceived = 0;
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlCopyMemory(SmartcardExtension->IoRequest.ReplyBuffer + sizeof(SCARD_IO_REQUEST),
                      T1->ReplyData,
                      T1->BytesReceived);
        return SmcCompleteReply(SmartcardExtension, SCARD_PROTOCOL_T1, T1->BytesReceived);
    }

    if (!(Pcb & 0x40))
    {
        if (T1->State == T1_I_BLOCK && T1->MoreData && ((Pcb >> 4) & 1) != T1->SSN)
        {
            T1->SSN ^= 1;
            T1->BytesSent += T1->InfBytesSent;
            T1->BytesToSend -= T1->InfBytesSent;
            T1->Resend = 0;
            return STATUS_MORE_PROCESSING_REQUIRED;
        }

        T1->Resend++;
        if (T1->Resend > T1_MAX_RETRIES)
            return SmcT1Resynch(T1);

        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    switch (Pcb)
    {
        case T1_IFS_RESPONSE:
            if (T1->State != T1_IFS_REQUEST)
                return SmcT1Fatal(T1);
            T1->State = T1_I_BLOCK;
            return STATUS_MORE_PROCESSING_REQUIRED;

        case T1_IFS_REQUEST:
            T1->IFSC = Block[3];
            T1->OriginalState = T1->State;
            T1->State = T1_IFS_RESPONSE;
            return STATUS_MORE_PROCESSING_REQUIRED;

        case T1_WTX_REQUEST:
            T1->Wtx = Block[3];
            T1->OriginalState = T1->State;
            T1->State = T1_WTX_RESPONSE;
            return STATUS_MORE_PROCESSING_REQUIRED;

        case T1_ABORT_REQUEST:
            T1->State = T1_ABORT_RESPONSE;
            return STATUS_MORE_PROCESSING_REQUIRED;

        case T1_VPP_ERROR:
            T1->State = T1_START;
            return STATUS_DEVICE_POWER_FAILURE;

        default:
            return SmcT1Fatal(T1);
    }
}
