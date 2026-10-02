/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite smart card driver library
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntstrsafe.h>
#include <smclib.h>

#define NDEBUG
#include <debug.h>

static CHAR Line[480];
static CHAR HexText[2][260];

static
PCSTR
Hex(
    _In_ ULONG Slot,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    static const CHAR Digits[] = "0123456789ABCDEF";
    PCHAR Out = HexText[Slot];
    ULONG Index;

    if (Length > 120)
        Length = 120;
    for (Index = 0; Index < Length; Index++)
    {
        *Out++ = Digits[Data[Index] >> 4];
        *Out++ = Digits[Data[Index] & 15];
    }
    *Out = ANSI_NULL;
    return HexText[Slot];
}

static
ULONG
Unhex(
    _In_ PCSTR Text,
    _Out_writes_bytes_(Size) PUCHAR Data,
    _In_ ULONG Size)
{
    ULONG Length = 0;
    UCHAR Value = 0;
    BOOLEAN High = TRUE;

    for (; *Text != ANSI_NULL && Length < Size; Text++)
    {
        UCHAR Digit;

        if (*Text >= '0' && *Text <= '9')
            Digit = *Text - '0';
        else if (*Text >= 'A' && *Text <= 'F')
            Digit = *Text - 'A' + 10;
        else
            continue;
        if (High)
            Value = Digit << 4;
        else
            Data[Length++] = Value | Digit;
        High = !High;
    }
    return Length;
}

typedef struct _SMC_REFERENCE
{
    PCSTR Label;
    PCSTR Value;
} SMC_REFERENCE;

#include "SmcLib_ref.h"

static ULONG NextReference;

static
VOID
Check(
    _In_ PCSTR Format,
    ...)
{
    CHAR Label[120];
    va_list Arguments;
    ULONG Index;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Label, sizeof(Label), Format, Arguments);
    va_end(Arguments);

    for (Index = NextReference; Index < RTL_NUMBER_OF(Reference); Index++)
    {
        if (strcmp(Reference[Index].Label, Label) == 0)
            break;
    }

    if (Index >= RTL_NUMBER_OF(Reference) || Reference[Index].Value == NULL)
    {
        ok(FALSE, "%s: got \"%s\"\n", Label, Line);
        return;
    }

    NextReference = Index + 1;
    ok(strcmp(Line, Reference[Index].Value) == 0, "%s: got \"%s\"\n", Label, Line);
}

static SMARTCARD_EXTENSION Extension;
static PDEVICE_OBJECT TestDevice;
static ULONG ClockRates[] = { 3571, 7143, 14285 };
static ULONG DataRates[] = { 9600, 19200, 38400, 57600, 115200 };

static
VOID
SetReader(
    _In_ ULONG Variant)
{
    PSCARD_READER_CAPABILITIES Reader = &Extension.ReaderCapabilities;

    RtlZeroMemory(Reader, sizeof(*Reader));
    Reader->SupportedProtocols = SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1;
    Reader->ReaderType = SCARD_READER_TYPE_SERIAL;
    Reader->MechProperties = 0;
    Reader->CurrentState = SCARD_PRESENT;
    Reader->Channel = 7;
    Reader->CLKFrequency.Default = 3571;
    Reader->CLKFrequency.Max = 3571;
    Reader->DataRate.Default = 9600;
    Reader->DataRate.Max = 9600;
    Reader->MaxIFSD = 254;
    if (Variant >= 1)
    {
        Reader->DataRate.Max = 115200;
        Reader->CLKFrequency.Max = 14285;
    }
    if (Variant >= 2)
    {
        Reader->DataRatesSupported.List = DataRates;
        Reader->DataRatesSupported.Entries = RTL_NUMBER_OF(DataRates);
        Reader->CLKFrequenciesSupported.List = ClockRates;
        Reader->CLKFrequenciesSupported.Entries = RTL_NUMBER_OF(ClockRates);
    }
    if (Variant == 3)
        Reader->SupportedProtocols = SCARD_PROTOCOL_T0;
    if (Variant == 4)
        Reader->MaxIFSD = 64;
    if (Variant == 5)
        Reader->MaxIFSD = 0;
    if (Variant == 6)
        Reader->MaxIFSD = 300;
}

static
BOOLEAN
Start(VOID)
{
    NTSTATUS Status;

    RtlZeroMemory(&Extension, sizeof(Extension));
    Extension.Version = SMCLIB_VERSION;
    Extension.SmartcardRequest.BufferSize = MIN_BUFFER_SIZE;
    Extension.SmartcardReply.BufferSize = MIN_BUFFER_SIZE;
    Status = SmartcardInitialize(&Extension);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;
    Extension.OsData->DeviceObject = TestDevice;
    SetReader(0);
    RtlCopyMemory(Extension.VendorAttr.VendorName.Buffer, "LiberNT", 7);
    Extension.VendorAttr.VendorName.Length = 7;
    RtlCopyMemory(Extension.VendorAttr.IfdType.Buffer, "KMT", 3);
    Extension.VendorAttr.IfdType.Length = 3;
    Extension.VendorAttr.UnitNo = 5;
    Extension.VendorAttr.IfdVersion.BuildNumber = 0x1234;
    Extension.VendorAttr.IfdVersion.VersionMinor = 2;
    Extension.VendorAttr.IfdVersion.VersionMajor = 1;
    RtlCopyMemory(Extension.VendorAttr.IfdSerialNo.Buffer, "SN42", 4);
    Extension.VendorAttr.IfdSerialNo.Length = 4;
    return TRUE;
}

static
VOID
PrintCard(
    _In_ ULONG Part)
{
    PSCARD_CARD_CAPABILITIES Card = &Extension.CardCapabilities;

    if (Part == 0)
    {
        RtlStringCbPrintfA(Line, sizeof(Line),
                           "inv=%u etu=%lu Fl=%u Dl=%u II=%u P=%u N=%u GT=%lu sup=%lx sel=%lx state=%lu atr=%u",
                           Card->InversConvention, Card->etu, Card->Fl, Card->Dl, Card->II, Card->P, Card->N,
                           Card->GT, Card->Protocol.Supported, Card->Protocol.Selected,
                           Extension.ReaderCapabilities.CurrentState, Card->ATR.Length);
    }
    else if (Part == 1)
    {
        RtlStringCbPrintfA(Line, sizeof(Line),
                           "WI=%u WT=%lu IFSC=%u CWI=%u BWI=%u EDC=%u CWT=%lu BWT=%lu BGT=%lu hist=%u:%s",
                           Card->T0.WI, Card->T0.WT, Card->T1.IFSC, Card->T1.CWI, Card->T1.BWI, Card->T1.EDC,
                           Card->T1.CWT, Card->T1.BWT, Card->T1.BGT, Card->HistoricalChars.Length,
                           Hex(0, Card->HistoricalChars.Buffer, min(Card->HistoricalChars.Length, 16)));
    }
    else
    {
        RtlStringCbPrintfA(Line, sizeof(Line),
                           "pts=%u Fl=%u Dl=%u clk=%lu rate=%lu stop=%u t1: ifsc=%u ifsd=%u state=%lu ssn=%u rsn=%u",
                           Card->PtsData.Type, Card->PtsData.Fl, Card->PtsData.Dl, Card->PtsData.CLKFrequency,
                           Card->PtsData.DataRate, Card->PtsData.StopBits, Extension.T1.IFSC, Extension.T1.IFSD,
                           Extension.T1.State, Extension.T1.SSN, Extension.T1.RSN);
    }
}

static
NTSTATUS
SetAtr(
    _In_ PCSTR Atr,
    _In_ ULONG Variant)
{
    PSCARD_CARD_CAPABILITIES Card = &Extension.CardCapabilities;

    SetReader(Variant);
    SmartcardInitializeCardCapabilities(&Extension);
    RtlZeroMemory(Card->ATR.Buffer, sizeof(Card->ATR.Buffer));
    Card->ATR.Length = (UCHAR)Unhex(Atr, Card->ATR.Buffer, sizeof(Card->ATR.Buffer));
    if (Atr[0] != ANSI_NULL && Atr[strlen(Atr) - 1] == '*')
    {
        UCHAR Check = 0;
        ULONG Index;

        for (Index = 1; Index < Card->ATR.Length; Index++)
            Check ^= Card->ATR.Buffer[Index];
        Card->ATR.Buffer[Card->ATR.Length++] = Check;
    }
    return SmartcardUpdateCardCapabilities(&Extension);
}

typedef struct _ATR_CASE
{
    PCSTR Atr;
    ULONG Variant;
} ATR_CASE;

static const ATR_CASE AtrCases[] =
{
    { "3B00", 0 },
    { "3B021450", 0 },
    { "3F00", 0 },
    { "03FF", 0 },
    { "3F021450", 0 },
    { "03BFD7F5", 0 },
    { "3B1011", 0 },
    { "3B1094", 0 },
    { "3B1094", 1 },
    { "3B1094", 2 },
    { "3B1018", 1 },
    { "3B1096", 2 },
    { "3B1097", 2 },
    { "3B101A", 2 },
    { "3B1071", 2 },
    { "3B10D5", 2 },
    { "3B1001", 0 },
    { "3B1010", 0 },
    { "3B2025", 0 },
    { "3B2000", 0 },
    { "3B207F", 0 },
    { "3B20E5", 0 },
    { "3B2045", 0 },
    { "3B20A5", 0 },
    { "3B20C5", 0 },
    { "3BA000205A", 0 },
    { "3B40FF", 0 },
    { "3B4002", 0 },
    { "3B40FE", 0 },
    { "3BC000400A", 0 },
    { "3BC0004001", 0 },
    { "3BC0004000", 0 },
    { "3BC00040FF", 0 },
    { "3BD094004005", 2 },
    { "3B8001*", 0 },
    { "3B8001*", 3 },
    { "3B8001*", 4 },
    { "3B8001*", 5 },
    { "3B8001*", 6 },
    { "3B80614501*", 0 },
    { "3B80E14501FE*", 0 },
    { "3B808001*", 0 },
    { "3B808100*", 0 },
    { "3B808000", 0 },
    { "3B80800000", 0 },
    { "3B808171FE4501*", 0 },
    { "3B808171FE4501*", 4 },
    { "3B808171204500*", 0 },
    { "3B80812123*", 0 },
    { "3B808121F0*", 0 },
    { "3B80812109*", 0 },
    { "3B80811100*", 0 },
    { "3B808111FF*", 0 },
    { "3B80814100*", 0 },
    { "3B808141FF*", 0 },
    { "3BC0FF01*", 0 },
    { "3BC00201*", 0 },
    { "3B909401*", 0 },
    { "3B90948171FE4501*", 0 },
    { "3B90948171FE4501*", 2 },
    { "3B90111000", 0 },
    { "3B90941000", 2 },
    { "3B90111101*", 0 },
    { "3B90111181*", 0 },
    { "3B90111010", 0 },
    { "3B90111001", 0 },
    { "3B9011100F", 0 },
    { "3B90111080", 0 },
    { "3B800100", 0 },
    { "3B8001", 0 },
    { "3B80", 0 },
    { "3B", 0 },
    { "", 0 },
    { "3A00", 0 },
    { "3B0F", 0 },
    { "3B0F0102030405060708090A0B0C0D0E0F", 0 },
    { "3B80801F03*", 0 },
    { "3B800E*", 0 },
    { "3B80800F*", 0 },
    { "3B8081010000", 0 },
    { "3B00FF", 0 },
    { "3B10", 0 },
    { "3B80811F03*", 0 },
    { "3B8002*", 0 },
    { "3B808102*", 0 },
};

static
VOID
TestAtr(VOID)
{
    ULONG Index, Part;
    NTSTATUS Status;

    for (Index = 0; Index < RTL_NUMBER_OF(AtrCases); Index++)
    {
        Status = SetAtr(AtrCases[Index].Atr, AtrCases[Index].Variant);
        RtlStringCbPrintfA(Line, sizeof(Line), "status=%lx", Status);
        Check("%lu %s/%lu", Index, AtrCases[Index].Atr, AtrCases[Index].Variant);
        for (Part = 0; Part < 3; Part++)
        {
            PrintCard(Part);
            Check("%lu %s/%lu", Index, AtrCases[Index].Atr, AtrCases[Index].Variant);
        }
    }
}

static UCHAR RequestData[600];
static UCHAR ReplyData[600];
static ULONG Information;

static
VOID
SetRequest(
    _In_ ULONG Protocol,
    _In_ ULONG PciLength,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ ULONG ReplyLength)
{
    PSCARD_IO_REQUEST Header = (PSCARD_IO_REQUEST)RequestData;

    RtlZeroMemory(RequestData, sizeof(RequestData));
    RtlFillMemory(ReplyData, sizeof(ReplyData), 0xEE);
    Header->dwProtocol = Protocol;
    Header->cbPciLength = PciLength;
    RtlCopyMemory(RequestData + PciLength, Data, Length);
    Information = 0xAAAAAAAA;
    Extension.IoRequest.Information = &Information;
    Extension.IoRequest.RequestBuffer = RequestData;
    Extension.IoRequest.RequestBufferLength = PciLength + Length;
    Extension.IoRequest.ReplyBuffer = ReplyData;
    Extension.IoRequest.ReplyBufferLength = ReplyLength;
}

typedef struct _T0_CASE
{
    PCSTR Apdu;
    ULONG Offset;
    ULONG PciLength;
} T0_CASE;

static const T0_CASE T0Cases[] =
{
    { "00A40000", 0, 8 },
    { "00B0000010", 0, 8 },
    { "00B0000000", 0, 8 },
    { "00D6000003AABBCC", 0, 8 },
    { "00A40400023F0005", 0, 8 },
    { "00A40400023F0000", 0, 8 },
    { "00D6000005AABB", 0, 8 },
    { "00D6000001AABBCCDD", 0, 8 },
    { "00D6000000", 3, 8 },
    { "00D6000003AABBCC", 3, 8 },
    { "00A400", 0, 8 },
    { "", 0, 8 },
    { "00D60000FF", 0, 8 },
    { "00D6000000AA", 0, 8 },
    { "00D6000003AABBCC", 270, 8 },
    { "00D6000003AABBCC", 271, 8 },
    { "00D6000003AABBCC", 272, 8 },
    { "00D6000003AABBCC", 274, 8 },
    { "00D6000003AABBCC", 276, 8 },
    { "00D6000003AABBCC", 277, 8 },
    { "00D6000003AABBCC", 278, 8 },
    { "00D6000003AABBCC", 279, 8 },
    { "00D6000003AABBCC", 280, 8 },
    { "00D6000003AABBCC", 281, 8 },
    { "00D6000003AABBCC", 285, 8 },
    { "00D6000003AABBCC", 288, 8 },
    { "00D6000003AABBCC", 289, 8 },
    { "00D6000003AABBCC", 300, 8 },
    { "00A40000", 278, 8 },
    { "00A40000", 279, 8 },
    { "00A40000", 280, 8 },
    { "00A40000", 281, 8 },
    { "00A40000", 282, 8 },
    { "00A40000", 283, 8 },
    { "00A40000", 284, 8 },
    { "00A40000", 285, 8 },
};

static
VOID
TestT0(VOID)
{
    UCHAR Data[300];
    ULONG Index, Length;
    NTSTATUS Status;

    Status = SetAtr("3B00", 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Extension.CardCapabilities.Protocol.Selected = SCARD_PROTOCOL_T0;

    for (Index = 0; Index < RTL_NUMBER_OF(T0Cases); Index++)
    {
        Length = Unhex(T0Cases[Index].Apdu, Data, sizeof(Data));
        SetRequest(SCARD_PROTOCOL_T0, T0Cases[Index].PciLength, Data, Length, sizeof(ReplyData));
        RtlFillMemory(Extension.SmartcardRequest.Buffer, Extension.SmartcardRequest.BufferSize, 0xDD);
        Extension.SmartcardRequest.BufferLength = T0Cases[Index].Offset;
        Extension.T0.Lc = 0x1111;
        Extension.T0.Le = 0x2222;
        Status = SmartcardT0Request(&Extension);
        RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx Lc=%lx Le=%lx len=%lu %s",
                           Status, Extension.T0.Lc, Extension.T0.Le, Extension.SmartcardRequest.BufferLength,
                           Hex(0, Extension.SmartcardRequest.Buffer + min(T0Cases[Index].Offset, 276), 12));
        Check("T0 %lu %s/%lu", Index, T0Cases[Index].Apdu, T0Cases[Index].Offset);
    }

    Length = Unhex("00B0000002", Data, sizeof(Data));
    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, sizeof(ReplyData));
    Extension.SmartcardRequest.BufferLength = 0;
    Status = SmartcardT0Request(&Extension);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Extension.SmartcardReply.BufferLength = Unhex("AABB9000", Extension.SmartcardReply.Buffer, 16);
    Status = SmartcardT0Reply(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx %s", Status, Information, Hex(0, ReplyData, 16));
    Check("t0 reply");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, 10);
    Extension.SmartcardReply.BufferLength = Unhex("AABB9000", Extension.SmartcardReply.Buffer, 16);
    Status = SmartcardT0Reply(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx %s", Status, Information, Hex(0, ReplyData, 16));
    Check("t0 reply small");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, sizeof(ReplyData));
    Extension.SmartcardReply.BufferLength = Unhex("90", Extension.SmartcardReply.Buffer, 16);
    Status = SmartcardT0Reply(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx %s", Status, Information, Hex(0, ReplyData, 16));
    Check("t0 reply one");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, sizeof(ReplyData));
    Extension.SmartcardReply.BufferLength = 0;
    Status = SmartcardT0Reply(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx %s", Status, Information, Hex(0, ReplyData, 16));
    Check("t0 reply empty");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, sizeof(ReplyData));
    Extension.SmartcardReply.BufferLength = Unhex("6C10", Extension.SmartcardReply.Buffer, 16);
    Status = SmartcardT0Reply(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx %s", Status, Information, Hex(0, ReplyData, 16));
    Check("t0 reply status");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, sizeof(ReplyData));
    Extension.SmartcardRequest.BufferLength = 0;
    Status = SmartcardRawRequest(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx len=%lu %s", Status, Extension.SmartcardRequest.BufferLength,
                       Hex(0, Extension.SmartcardRequest.Buffer, 16));
    Check("raw request");
    Extension.SmartcardReply.BufferLength = Unhex("AABB9000", Extension.SmartcardReply.Buffer, 16);
    Status = SmartcardRawReply(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx %s", Status, Information, Hex(0, ReplyData, 16));
    Check("raw reply");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, 4);
    Extension.SmartcardReply.BufferLength = Unhex("90", Extension.SmartcardReply.Buffer, 16);
    Status = SmartcardT0Reply(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx %s", Status, Information, Hex(0, ReplyData, 16));
    Check("t0 reply one small");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, 2);
    Extension.SmartcardReply.BufferLength = Unhex("AABB9000", Extension.SmartcardReply.Buffer, 16);
    Status = SmartcardRawReply(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx %s", Status, Information, Hex(0, ReplyData, 16));
    Check("raw reply small");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, sizeof(ReplyData));
    Extension.SmartcardRequest.BufferLength = 275;
    Status = SmartcardRawRequest(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx len=%lu", Status, Extension.SmartcardRequest.BufferLength);
    Check("raw request full");

    SetRequest(SCARD_PROTOCOL_T0, 8, Data, Length, sizeof(ReplyData));
    Extension.SmartcardRequest.BufferLength = 274;
    Status = SmartcardRawRequest(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx len=%lu", Status, Extension.SmartcardRequest.BufferLength);
    Check("raw request fits");
}

static NTSTATUS Ioctl(_In_ PIRP Irp, _In_ ULONG Code, _In_reads_bytes_(InputLength) const VOID *Input,
                      _In_ ULONG InputLength, _In_ ULONG OutputLength);

typedef struct _T1_STEP
{
    PCSTR CardReply;
} T1_STEP;

static
ULONG
AddEdc(
    _Inout_updates_bytes_(Length + 2) PUCHAR Block,
    _In_ ULONG Length,
    _In_ BOOLEAN Break)
{
    UCHAR Lrc = 0;
    ULONG Index, Bit;

    if (Extension.CardCapabilities.T1.EDC & T1_CRC_CHECK)
    {
        USHORT Crc = 0;

        for (Index = 0; Index < Length; Index++)
        {
            Crc ^= Block[Index];
            for (Bit = 0; Bit < 8; Bit++)
                Crc = (Crc & 1) ? (USHORT)((Crc >> 1) ^ 0xA001) : (USHORT)(Crc >> 1);
        }
        if (Break)
            Crc = (USHORT)~Crc;
        Block[Length] = (UCHAR)(Crc >> 8);
        Block[Length + 1] = (UCHAR)Crc;
        return Length + 2;
    }

    for (Index = 0; Index < Length; Index++)
        Lrc ^= Block[Index];
    Block[Length] = Break ? (UCHAR)~Lrc : Lrc;
    return Length + 1;
}

static
VOID
PrintT1(
    _In_ NTSTATUS Status)
{
    PT1_DATA T1 = &Extension.T1;

    RtlStringCbPrintfA(Line, sizeof(Line),
                       "st=%lx %s s=%lx/%lx ssn=%u rsn=%u ifs=%u/%u snt=%lu tos=%lu rcv=%lu m=%u rs=%u ry=%u w=%u wr=%u e=%u i=%u",
                       Status,
                       Hex(0, Extension.SmartcardRequest.Buffer, min(Extension.SmartcardRequest.BufferLength, 44)),
                       T1->State, T1->OriginalState, T1->SSN, T1->RSN, T1->IFSC, T1->IFSD, T1->BytesSent,
                       T1->BytesToSend, T1->BytesReceived, T1->MoreData, T1->Resend, T1->Resynch, T1->Wtx,
                       T1->WaitForReply, T1->LastError, T1->InfBytesSent);
}

static
VOID
RunT1(
    _In_ PCSTR Name,
    _In_ PCSTR Atr,
    _In_ ULONG Variant,
    _In_ ULONG ApduLength,
    _In_ ULONG ReplyLength,
    _In_reads_(Count) const T1_STEP *Steps,
    _In_ ULONG Count)
{
    UCHAR Data[600];
    ULONG Index, Length;
    NTSTATUS Status;

    if (Atr != NULL)
    {
        Status = SetAtr(Atr, Variant & 15);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Variant & 0x100)
            Extension.T1.NAD = 0x21;
        Extension.CardCapabilities.Protocol.Selected = SCARD_PROTOCOL_T1;
        Extension.ReaderCapabilities.CurrentState = SCARD_SPECIFIC;
    }

    for (Index = 0; Index < ApduLength; Index++)
        Data[Index] = (UCHAR)(Index + 1);
    SetRequest(SCARD_PROTOCOL_T1, sizeof(SCARD_IO_REQUEST), Data, ApduLength, ReplyLength);

    for (Index = 0; Index < Count; Index++)
    {
        RtlFillMemory(Extension.SmartcardRequest.Buffer, Extension.SmartcardRequest.BufferSize, 0xDD);
        Extension.SmartcardRequest.BufferLength = (Variant & 0x400) ? 283 : ((Variant & 0x200) ? 2 : 0);
        Status = SmartcardT1Request(&Extension);
        PrintT1(Status);
        Check("%s %lu request", Name, Index);
        if (!NT_SUCCESS(Status) || Steps[Index].CardReply == NULL)
            break;

        Length = Unhex(Steps[Index].CardReply, Extension.SmartcardReply.Buffer, 280);
        if (Steps[Index].CardReply[0] == '!')
            Length = AddEdc(Extension.SmartcardReply.Buffer, Length, TRUE);
        else if (Steps[Index].CardReply[0] != '=')
            Length = AddEdc(Extension.SmartcardReply.Buffer, Length, FALSE);
        Extension.SmartcardReply.BufferLength = Length;
        Status = SmartcardT1Reply(&Extension);
        RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx info=%lx s=%lx ssn=%u rsn=%u ifsc=%u rcv=%lu w=%u e=%u rs=%u ry=%u %s",
                           Status, Information, Extension.T1.State, Extension.T1.SSN, Extension.T1.RSN,
                           Extension.T1.IFSC, Extension.T1.BytesReceived, Extension.T1.Wtx, Extension.T1.LastError,
                           Extension.T1.Resend, Extension.T1.Resynch, Hex(0, ReplyData, 20));
        Check("%s %lu reply", Name, Index);
        if (Status != STATUS_MORE_PROCESSING_REQUIRED)
            break;
    }
}

static const T1_STEP T1Simple[] =
{
    { "00E101FE" },
    { "0000029000" },
    { NULL },
};

static const T1_STEP T1Second[] =
{
    { "0040029000" },
};

static const T1_STEP T1Third[] =
{
    { "0000026A82" },
};

static const T1_STEP T1ChainOut[] =
{
    { "00E101FE" },
    { "009000" },
    { "008000" },
    { "009000" },
    { "0000029000" },
};

static const T1_STEP T1ChainIn[] =
{
    { "00E101FE" },
    { "002004A1A2A3A4" },
    { "006004B1B2B3B4" },
    { "000003C19000" },
};

static const T1_STEP T1Wtx[] =
{
    { "00E101FE" },
    { "00C30105" },
    { "0000029000" },
};

static const T1_STEP T1IfsRequest[] =
{
    { "00E101FE" },
    { "00C10140" },
    { "0000029000" },
};

static const T1_STEP T1BadEdc[] =
{
    { "00E101FE" },
    { "!0000029000" },
    { "!0000029000" },
    { "!0000029000" },
    { "!0000029000" },
    { "00E000" },
    { "00E101FE" },
    { "0000029000" },
};

static const T1_STEP T1AllBad[] =
{
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
};

static const T1_STEP T1CardError[] =
{
    { "00E101FE" },
    { "008100" },
    { "008200" },
    { "008000" },
    { "0000029000" },
};

static const T1_STEP T1Sequence[] =
{
    { "00E101FE" },
    { "0040029000" },
    { "0000029000" },
};

static const T1_STEP T1Abort[] =
{
    { "00E101FE" },
    { "00C200" },
    { "0000029000" },
};

static const T1_STEP T1Short[] =
{
    { "00E101FE" },
    { "=0000" },
    { "=" },
    { "0000029000" },
};

static const T1_STEP T1Nad[] =
{
    { "00E101FE" },
    { "1200029000" },
    { "0000029000" },
};

static const T1_STEP T1Vpp[] =
{
    { "00E101FE" },
    { "00E400" },
    { "0000029000" },
};

static const T1_STEP T1IfsRefused[] =
{
    { "00E10120" },
    { "0000029000" },
};

static const T1_STEP T1Crc[] =
{
    { "=00E101FE0000" },
    { "=00000290000000" },
};

static const T1_STEP T1Overflow[] =
{
    { "00E101FE" },
    { "0000080102030405069000" },
};

static const T1_STEP T1Resend[] =
{
    { "00E101FE" },
    { "008000" },
    { "009000" },
    { "008000" },
    { "0000029000" },
};

static const T1_STEP T1Recover[] =
{
    { "00E101FE" },
    { "!0000029000" },
    { "!0000029000" },
    { "!0000029000" },
    { "00E000" },
    { "0000029000" },
};

static const T1_STEP T1RecoverIfs[] =
{
    { "!00E101FE" },
    { "!00E101FE" },
    { "!00E101FE" },
    { "00E000" },
    { "00E101FE" },
    { "0000029000" },
};

static const T1_STEP T1ChainWtx[] =
{
    { "00E101FE" },
    { "00C30102" },
    { "009000" },
    { "!008000" },
    { "008000" },
    { "00C30103" },
    { "0000029000" },
};

static const T1_STEP T1ChainInBad[] =
{
    { "00E101FE" },
    { "002004A1A2A3A4" },
    { "!006004B1B2B3B4" },
    { "002004A1A2A3A4" },
    { "006004B1B2B3B4" },
    { "000003C19000" },
};

static const T1_STEP T1Length[] =
{
    { "00E101FE" },
    { "=0000059000FF" },
    { "=000001900091" },
    { "0000029000" },
};

static const T1_STEP T1ChainAbort[] =
{
    { "00E101FE" },
    { "00C200" },
    { "0000029000" },
};

static const T1_STEP T1Unexpected[] =
{
    { "00E101FE" },
    { "00E30101" },
    { "00C400" },
    { "00FF00" },
    { "0000029000" },
};

static const T1_STEP T1Keep[] =
{
    { "008000" },
    { "0040029000" },
};

static const T1_STEP T1ChainInWtx[] =
{
    { "00E101FE" },
    { "002004A1A2A3A4" },
    { "00C30104" },
    { "004003C19000" },
};

static const T1_STEP T1ChainInR[] =
{
    { "00E101FE" },
    { "002004A1A2A3A4" },
    { "009000" },
    { "008000" },
    { "004003C19000" },
};

static const T1_STEP T1IfsI[] =
{
    { "0000029000" },
    { "0000029000" },
};

static const T1_STEP T1BadS[] =
{
    { "00E101FE" },
    { "00C100" },
    { "00C3020101" },
    { "0000029000" },
};

static const T1_STEP T1NoIfs[] =
{
    { "0000029000" },
};

static const T1_STEP T1Big[] =
{
    { "00E101FE" },
    { "009000" },
    { "0000029000" },
};

static const T1_STEP T1CrcRaw[] =
{
    { "=00E101FE0000" },
    { "=00E101FE0000" },
};

static
VOID
TestT1(VOID)
{
    UCHAR Input[4];
    PIRP Irp;

    RunT1("simple", "3B800181", 0, 5, 500, T1Simple, RTL_NUMBER_OF(T1Simple));
    RunT1("second", NULL, 0, 4, 500, T1Second, RTL_NUMBER_OF(T1Second));
    RunT1("third", NULL, 0, 6, 500, T1Third, RTL_NUMBER_OF(T1Third));
    RunT1("empty", "3B800181", 0, 0, 500, T1Simple, 2);
    RunT1("chainout", "3B800181", 0, 70, 500, T1ChainOut, RTL_NUMBER_OF(T1ChainOut));
    RunT1("chainin", "3B800181", 0, 5, 500, T1ChainIn, RTL_NUMBER_OF(T1ChainIn));
    RunT1("wtx", "3B800181", 0, 5, 500, T1Wtx, RTL_NUMBER_OF(T1Wtx));
    RunT1("ifs", "3B800181", 0, 70, 500, T1IfsRequest, RTL_NUMBER_OF(T1IfsRequest));
    RunT1("ifskeep", NULL, 0, 70, 500, T1Keep, RTL_NUMBER_OF(T1Keep));
    RunT1("badedc", "3B800181", 0, 5, 500, T1BadEdc, RTL_NUMBER_OF(T1BadEdc));
    RunT1("allbad", "3B800181", 0, 5, 500, T1AllBad, RTL_NUMBER_OF(T1AllBad));
    RunT1("afterfail", NULL, 0, 5, 500, T1Simple, RTL_NUMBER_OF(T1Simple));
    RunT1("carderror", "3B800181", 0, 5, 500, T1CardError, RTL_NUMBER_OF(T1CardError));
    RunT1("sequence", "3B800181", 0, 5, 500, T1Sequence, RTL_NUMBER_OF(T1Sequence));
    RunT1("abort", "3B800181", 0, 5, 500, T1Abort, RTL_NUMBER_OF(T1Abort));
    RunT1("short", "3B800181", 0, 5, 500, T1Short, RTL_NUMBER_OF(T1Short));
    RunT1("nad", "3B800181", 0x100, 5, 500, T1Nad, RTL_NUMBER_OF(T1Nad));
    RunT1("vpp", "3B800181", 0, 5, 500, T1Vpp, RTL_NUMBER_OF(T1Vpp));
    RunT1("ifsrefused", "3B800181", 0, 5, 500, T1IfsRefused, RTL_NUMBER_OF(T1IfsRefused));
    RunT1("crc", "3B808171204501*", 0, 5, 500, T1CrcRaw, RTL_NUMBER_OF(T1CrcRaw));
    RunT1("overflow", "3B800181", 0, 5, 12, T1Overflow, RTL_NUMBER_OF(T1Overflow));
    RunT1("ifsc254", "3B808171FE4500*", 0, 300, 500, T1Big, RTL_NUMBER_OF(T1Big));
    RunT1("ifsd64", "3B800181", 4, 5, 500, T1Simple, RTL_NUMBER_OF(T1Simple));
    RunT1("offset", "3B800181", 0x200, 5, 500, T1Simple, RTL_NUMBER_OF(T1Simple));
    RunT1("resend", "3B800181", 0, 70, 500, T1Resend, RTL_NUMBER_OF(T1Resend));
    RunT1("recover", "3B800181", 0, 5, 500, T1Recover, RTL_NUMBER_OF(T1Recover));
    RunT1("recovernext", NULL, 0, 4, 500, T1Second, RTL_NUMBER_OF(T1Second));
    RunT1("recoverifs", "3B800181", 0, 5, 500, T1RecoverIfs, RTL_NUMBER_OF(T1RecoverIfs));
    RunT1("chainwtx", "3B800181", 0, 70, 500, T1ChainWtx, RTL_NUMBER_OF(T1ChainWtx));
    RunT1("chaininbad", "3B800181", 0, 5, 500, T1ChainInBad, RTL_NUMBER_OF(T1ChainInBad));
    RunT1("length", "3B800181", 0, 5, 500, T1Length, RTL_NUMBER_OF(T1Length));
    RunT1("chainabort", "3B800181", 0, 70, 500, T1ChainAbort, RTL_NUMBER_OF(T1ChainAbort));
    RunT1("unexpected", "3B800181", 0, 5, 500, T1Unexpected, RTL_NUMBER_OF(T1Unexpected));
    RunT1("t01card", "3B808001*", 0, 5, 500, T1Simple, RTL_NUMBER_OF(T1Simple));
    RunT1("bigapdu", "3B800181", 0, 300, 500, T1Simple, 1);
    RunT1("ifsd0", "3B800181", 5, 5, 500, T1Simple, RTL_NUMBER_OF(T1Simple));
    RunT1("ifsd300", "3B800181", 6, 5, 500, T1Simple, RTL_NUMBER_OF(T1Simple));
    RunT1("chaininwtx", "3B800181", 0, 5, 500, T1ChainInWtx, RTL_NUMBER_OF(T1ChainInWtx));
    RunT1("chaininr", "3B800181", 0, 5, 500, T1ChainInR, RTL_NUMBER_OF(T1ChainInR));
    RunT1("ifsi", "3B800181", 0, 5, 500, T1IfsI, RTL_NUMBER_OF(T1IfsI));
    RunT1("bads", "3B800181", 0, 5, 500, T1BadS, RTL_NUMBER_OF(T1BadS));
    RunT1("crcok", "3B808171204501*", 0, 5, 500, T1Simple, RTL_NUMBER_OF(T1Simple));
    RunT1("full", "3B800181", 0x400, 5, 500, T1Simple, 1);

    Irp = IoAllocateIrp(2, FALSE);
    ok(Irp != NULL, "No IRP\n");
    if (Irp != NULL)
    {
        SetAtr("3B800181", 0);
        Extension.CardCapabilities.Protocol.Selected = SCARD_PROTOCOL_T1;
        Extension.ReaderCapabilities.CurrentState = SCARD_SPECIFIC;
        *(PULONG)Input = SCARD_ATTR_SUPRESS_T1_IFS_REQUEST;
        Ioctl(Irp, IOCTL_SMARTCARD_SET_ATTRIBUTE, Input, 4, 0);
        Check("suppress");
        RunT1("noifs", NULL, 0, 5, 500, T1NoIfs, RTL_NUMBER_OF(T1NoIfs));
        IoReuseIrp(Irp, STATUS_SUCCESS);
        IoFreeIrp(Irp);
    }
}

static ULONG CallbackIndex;
static NTSTATUS CallbackStatus;
static CHAR CallbackText[300];
static PIRP CallbackIrp;
static BOOLEAN CallbackAnswer;
static BOOLEAN KeepNotification;
static NTSTATUS ParseStatus;

static
NTSTATUS
Callback(
    _In_ ULONG Index,
    _In_ PSMARTCARD_EXTENSION SmartcardExtension)
{
    ULONG Length = SmartcardExtension->IoRequest.RequestBufferLength;

    PSCARD_CARD_CAPABILITIES Card = &SmartcardExtension->CardCapabilities;
    size_t Used = strlen(CallbackText);

    CallbackIndex = Index;
    RtlStringCbPrintfA(CallbackText + Used, sizeof(CallbackText) - Used,
                       " cb=%lu@%u mn=%lx in=%lu:%s out=%lu irp=%u%u s=%lu p=%lx pts=%u/%u/%u/%lu/%lu",
                       Index, KeGetCurrentIrql(), SmartcardExtension->MinorIoControlCode,
                       Length, Hex(1, SmartcardExtension->IoRequest.RequestBuffer, min(Length, 12)),
                       SmartcardExtension->IoRequest.ReplyBufferLength,
                       SmartcardExtension->OsData->CurrentIrp == CallbackIrp,
                       SmartcardExtension->OsData->NotificationIrp == CallbackIrp,
                       SmartcardExtension->ReaderCapabilities.CurrentState, Card->Protocol.Selected,
                       Card->PtsData.Type, Card->PtsData.Fl, Card->PtsData.Dl, Card->PtsData.CLKFrequency,
                       Card->PtsData.DataRate);
    if (CallbackAnswer && NT_SUCCESS(CallbackStatus) && SmartcardExtension->IoRequest.ReplyBufferLength >= 4)
    {
        RtlCopyMemory(SmartcardExtension->IoRequest.ReplyBuffer, "\x3B\x02\x14\x50", 4);
        *SmartcardExtension->IoRequest.Information = 4;
    }
    return Index == RDF_ATR_PARSE ? ParseStatus : CallbackStatus;
}

#define DEFINE_CALLBACK(Index) \
    static NTSTATUS NTAPI Callback##Index(PSMARTCARD_EXTENSION SmartcardExtension) \
    { return Callback(Index, SmartcardExtension); }

DEFINE_CALLBACK(0)
DEFINE_CALLBACK(1)
DEFINE_CALLBACK(2)
DEFINE_CALLBACK(3)
DEFINE_CALLBACK(4)
DEFINE_CALLBACK(5)
DEFINE_CALLBACK(6)
DEFINE_CALLBACK(7)
DEFINE_CALLBACK(8)
DEFINE_CALLBACK(9)

static BOOLEAN Completed;
static NTSTATUS CompletedStatus;
static ULONG_PTR CompletedInformation;

static
NTSTATUS
NTAPI
CompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);

    Completed = TRUE;
    CompletedStatus = Irp->IoStatus.Status;
    CompletedInformation = Irp->IoStatus.Information;
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static UCHAR SystemBuffer[128];

static
NTSTATUS
Ioctl(
    _In_ PIRP Irp,
    _In_ ULONG Code,
    _In_reads_bytes_(InputLength) const VOID *Input,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength)
{
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;
    KIRQL Irql;

    IoReuseIrp(Irp, STATUS_NOT_SUPPORTED);
    RtlFillMemory(SystemBuffer, sizeof(SystemBuffer), 0xEE);
    if (InputLength != 0)
        RtlCopyMemory(SystemBuffer, Input, InputLength);
    Irp->AssociatedIrp.SystemBuffer = SystemBuffer;
    Irp->IoStatus.Information = 0x55;

    IoSetCompletionRoutine(Irp, CompletionRoutine, NULL, TRUE, TRUE, TRUE);
    IoSetNextIrpStackLocation(Irp);
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = Code;
    Stack->Parameters.DeviceIoControl.InputBufferLength = InputLength;
    Stack->Parameters.DeviceIoControl.OutputBufferLength = OutputLength;
    IoSetNextIrpStackLocation(Irp);

    Completed = FALSE;
    CompletedStatus = 0x77777777;
    CompletedInformation = 0x77;
    CallbackIndex = 99;
    CallbackText[0] = ANSI_NULL;
    CallbackIrp = Irp;
    Status = SmartcardDeviceControl(&Extension, Irp);
    RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx done=%u ios=%lx info=%lx out=%s cur=%u not=%u state=%lu sel=%lx mn=%lx atr=%u t1=%lx |%s",
                       Status, Completed, CompletedStatus, (ULONG)CompletedInformation, Hex(0, SystemBuffer, 10),
                       Extension.OsData->CurrentIrp != NULL, Extension.OsData->NotificationIrp != NULL,
                       Extension.ReaderCapabilities.CurrentState, Extension.CardCapabilities.Protocol.Selected,
                       Extension.MinorIoControlCode, Extension.CardCapabilities.ATR.Length, Extension.T1.State,
                       CallbackText);
    if (!KeepNotification)
    {
        KeAcquireSpinLock(&Extension.OsData->SpinLock, &Irql);
        Extension.OsData->NotificationIrp = NULL;
        KeReleaseSpinLock(&Extension.OsData->SpinLock, Irql);
    }
    return Status;
}

typedef struct _IOCTL_CASE
{
    ULONG Code;
    ULONG Input;
    ULONG InputLength;
    ULONG OutputLength;
    ULONG State;
    NTSTATUS Answer;
    ULONG Flags;
    PCSTR Atr;
    ULONG Variant;
} IOCTL_CASE;

#define IF_NO_CALLBACKS 1
#define IF_SELECTED_T0  2
#define IF_SELECTED_T1  4
#define IF_ANSWER       8
#define IF_PARSE_FAILS  16

#define IOCTL_VENDOR SCARD_CTL_CODE(2050)
#define IOCTL_OTHER  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

static const IOCTL_CASE IoctlCases[] =
{
    { IOCTL_SMARTCARD_GET_STATE, 0, 0, 4, SCARD_PRESENT, 0, 0, NULL, 2 },
    { IOCTL_SMARTCARD_GET_STATE, 0, 0, 3, SCARD_PRESENT, 0, 0, NULL, 2 },
    { IOCTL_SMARTCARD_GET_STATE, 0, 0, 8, SCARD_SPECIFIC, 0, IF_SELECTED_T1, NULL, 2 },
    { IOCTL_SMARTCARD_GET_STATE, 0, 4, 4, SCARD_ABSENT, 0, 0, NULL, 2 },
    { IOCTL_SMARTCARD_GET_STATE, 0, 0, 4, SCARD_UNKNOWN, 0, 0, NULL, 2 },

    { IOCTL_SMARTCARD_IS_PRESENT, 0, 0, 0, SCARD_PRESENT, STATUS_PENDING, 0, NULL, 2 },
    { IOCTL_SMARTCARD_IS_PRESENT, 0, 0, 0, SCARD_SWALLOWED, STATUS_PENDING, 0, NULL, 2 },
    { IOCTL_SMARTCARD_IS_PRESENT, 0, 0, 0, SCARD_ABSENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_IS_PRESENT, 0, 0, 0, SCARD_ABSENT, STATUS_UNSUCCESSFUL, 0, NULL, 2 },
    { IOCTL_SMARTCARD_IS_PRESENT, 0, 0, 0, SCARD_UNKNOWN, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_IS_PRESENT, 0, 0, 0, SCARD_ABSENT, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },
    { IOCTL_SMARTCARD_IS_ABSENT, 0, 0, 0, SCARD_ABSENT, STATUS_PENDING, 0, NULL, 2 },
    { IOCTL_SMARTCARD_IS_ABSENT, 0, 0, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_IS_ABSENT, 0, 0, 0, SCARD_SPECIFIC, STATUS_UNSUCCESSFUL, 0, NULL, 2 },

    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_PRESENT, STATUS_SUCCESS, IF_ANSWER, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_WARM_RESET, 4, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_ANSWER | IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_POWER_DOWN, 4, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_POWER_DOWN, 4, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, 3, 4, 64, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 3, 64, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 32, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 33, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_ABSENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_UNKNOWN, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_PRESENT, STATUS_IO_TIMEOUT, 0, NULL, 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_PRESENT, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },

    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, 0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, 0x8, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 3, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 3, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_ABSENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_POWERED, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T1, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_IO_TIMEOUT, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },

    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T1, 12, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T1, 12, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T1, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_ABSENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 7, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 8, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 0, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_SPECIFIC, STATUS_IO_TIMEOUT, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0 | IF_NO_CALLBACKS, NULL, 2 },

    { IOCTL_SMARTCARD_EJECT, 0, 0, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_EJECT, 0, 0, 0, SCARD_ABSENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_EJECT, 0, 0, 0, SCARD_PRESENT, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },
    { IOCTL_SMARTCARD_SWALLOW, 0, 0, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SWALLOW, 0, 0, 0, SCARD_PRESENT, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },
    { IOCTL_SMARTCARD_CONFISCATE, 0, 0, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_CONFISCATE, 0, 0, 0, SCARD_PRESENT, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },
    { IOCTL_SMARTCARD_GET_LAST_ERROR, 0, 0, 4, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_GET_PERF_CNTR, 0, 0, 64, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_READ, 0, 4, 4, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_WRITE, 0, 4, 4, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_VENDOR, 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_VENDOR, 0x11223344, 4, 8, SCARD_PRESENT, STATUS_IO_TIMEOUT, 0, NULL, 2 },
    { IOCTL_VENDOR, 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },
    { IOCTL_OTHER, 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_OTHER, 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },

    { IOCTL_SMARTCARD_SET_ATTRIBUTE, SCARD_ATTR_SUPRESS_T1_IFS_REQUEST, 4, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_ATTRIBUTE, SCARD_ATTR_SUPRESS_T1_IFS_REQUEST, 8, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_ATTRIBUTE, SCARD_ATTR_SUPRESS_T1_IFS_REQUEST, 3, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_ATTRIBUTE, SCARD_ATTR_VENDOR_NAME, 8, 0, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_ATTRIBUTE, SCARD_ATTR_VENDOR_NAME, 8, 0, SCARD_PRESENT, STATUS_SUCCESS, IF_NO_CALLBACKS, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, 0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, 0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, 0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, 0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B909401*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT | SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, 0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 0 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 1 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 4 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1018", 0 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1018", 1 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1018", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1018", 4 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1096", 0 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1096", 1 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1096", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1096", 4 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1097", 0 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1097", 1 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1097", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1097", 4 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B10D5", 0 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B10D5", 1 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B10D5", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B10D5", 4 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90948171FE4501*", 0 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90948171FE4501*", 1 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90948171FE4501*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90948171FE4501*", 4 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1011", 0 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1011", 1 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1011", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1011", 4 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_UNKNOWN, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_UNKNOWN, STATUS_SUCCESS, IF_ANSWER, "3B808001*", 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_UNKNOWN, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_ABSENT, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_ABSENT, STATUS_SUCCESS, IF_ANSWER, "3B808001*", 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_ABSENT, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_PRESENT, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_PRESENT, STATUS_SUCCESS, IF_ANSWER, "3B808001*", 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_PRESENT, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_SWALLOWED, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_SWALLOWED, STATUS_SUCCESS, IF_ANSWER, "3B808001*", 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_SWALLOWED, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_POWERED, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_POWERED, STATUS_SUCCESS, IF_ANSWER, "3B808001*", 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_POWERED, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_ANSWER, "3B808001*", 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_SPECIFIC, STATUS_SUCCESS, 0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 4, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_ANSWER, "3B808001*", 2 },
    { IOCTL_SMARTCARD_TRANSMIT, SCARD_PROTOCOL_T0, 12, 64, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_ANSWER, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T0, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 8, SCARD_SPECIFIC, STATUS_SUCCESS, IF_SELECTED_T1, "3B808001*", 2 },
    { IOCTL_SMARTCARD_POWER, SCARD_COLD_RESET, 3, 64, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { SCARD_CTL_CODE(13), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { SCARD_CTL_CODE(17), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { SCARD_CTL_CODE(18), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { SCARD_CTL_CODE(100), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { SCARD_CTL_CODE(2047), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { SCARD_CTL_CODE(2048), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { SCARD_CTL_CODE(3400), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { SCARD_CTL_CODE(4095), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { CTL_CODE(FILE_DEVICE_SMARTCARD, 2100, METHOD_NEITHER, FILE_ANY_ACCESS), 0x11223344, 4, 8, SCARD_PRESENT, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_RAW, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_PARSE_FAILS, "3B808001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_DEFAULT, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_PARSE_FAILS, "3B808001*", 2 },
    { IOCTL_SMARTCARD_IS_ABSENT, 0, 0, 0, SCARD_UNKNOWN, STATUS_SUCCESS, 0, NULL, 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90111001", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1018", 5 },
};

static const IOCTL_CASE LateCases[] =
{
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1094", 3 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1018", 3 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1096", 3 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1097", 3 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B10D5", 3 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90948171FE4501*", 3 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B1011", 3 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_SELECTED_T1, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90111001", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90111001", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_SELECTED_T1, "3B8001*", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0, 4, 4, SCARD_POWERED, STATUS_SUCCESS, IF_SELECTED_T1, "3B021450", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B90941001", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, IF_SELECTED_T1, "3B90941000", 2 },
    { IOCTL_SMARTCARD_SET_PROTOCOL, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, 4, 4, SCARD_NEGOTIABLE, STATUS_SUCCESS, 0, "3B9011900101*", 2 },
};

static const ULONG Attributes[] =
{
    SCARD_ATTR_VENDOR_NAME, SCARD_ATTR_VENDOR_IFD_TYPE, SCARD_ATTR_VENDOR_IFD_VERSION,
    SCARD_ATTR_VENDOR_IFD_SERIAL_NO, SCARD_ATTR_CHANNEL_ID, SCARD_ATTR_PROTOCOL_TYPES,
    SCARD_ATTR_DEFAULT_CLK, SCARD_ATTR_MAX_CLK, SCARD_ATTR_DEFAULT_DATA_RATE, SCARD_ATTR_MAX_DATA_RATE,
    SCARD_ATTR_MAX_IFSD, SCARD_ATTR_SYNC_PROTOCOL_TYPES, SCARD_ATTR_POWER_MGMT_SUPPORT,
    SCARD_ATTR_USER_TO_CARD_AUTH_DEVICE, SCARD_ATTR_USER_AUTH_INPUT_DEVICE, SCARD_ATTR_CHARACTERISTICS,
    SCARD_ATTR_CURRENT_PROTOCOL_TYPE, SCARD_ATTR_CURRENT_CLK, SCARD_ATTR_CURRENT_F, SCARD_ATTR_CURRENT_D,
    SCARD_ATTR_CURRENT_N, SCARD_ATTR_CURRENT_W, SCARD_ATTR_CURRENT_IFSC, SCARD_ATTR_CURRENT_IFSD,
    SCARD_ATTR_CURRENT_BWT, SCARD_ATTR_CURRENT_CWT, SCARD_ATTR_CURRENT_EBC_ENCODING, SCARD_ATTR_EXTENDED_BWT,
    SCARD_ATTR_ICC_PRESENCE, SCARD_ATTR_ICC_INTERFACE_STATUS, SCARD_ATTR_CURRENT_IO_STATE,
    SCARD_ATTR_ATR_STRING, SCARD_ATTR_ICC_TYPE_PER_ATR, SCARD_ATTR_ESC_RESET, SCARD_ATTR_MAXINPUT,
    SCARD_ATTR_DEVICE_UNIT, SCARD_ATTR_DEVICE_IN_USE, SCARD_ATTR_DEVICE_FRIENDLY_NAME_A,
    SCARD_ATTR_DEVICE_SYSTEM_NAME_A, SCARD_ATTR_DEVICE_FRIENDLY_NAME_W, SCARD_ATTR_DEVICE_SYSTEM_NAME_W,
    SCARD_ATTR_SUPRESS_T1_IFS_REQUEST, SCARD_PERF_NUM_TRANSMISSIONS, 0x12345678,
};

static
VOID
SetCallbacks(
    _In_ BOOLEAN Enable)
{
    static NTSTATUS (NTAPI *const Callbacks[])(PSMARTCARD_EXTENSION) =
    {
        Callback0, Callback1, Callback2, Callback3, Callback4,
        Callback5, Callback6, Callback7, Callback8, Callback9,
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Callbacks); Index++)
        Extension.ReaderFunction[Index] = Enable ? Callbacks[Index] : NULL;
}

static
VOID
TestIoctlStates(
    _In_ PIRP Irp,
    _In_ PIRP SecondIrp)
{
    ULONG Index, Pass;
    UCHAR Input[16];
    NTSTATUS Status;
    KIRQL Irql;

    for (Pass = 0; Pass < 4; Pass++)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Attributes); Index++)
        {
            Status = SetAtr("3B90948171FE4501*", 2);
            ok_eq_hex(Status, STATUS_SUCCESS);
            SetCallbacks(Pass != 3);
            CallbackStatus = STATUS_SUCCESS;
            CallbackAnswer = FALSE;
            if (Pass >= 1)
            {
                Extension.ReaderCapabilities.CurrentState = SCARD_SPECIFIC;
                Extension.CardCapabilities.Protocol.Selected = SCARD_PROTOCOL_T1;
            }
            *(PULONG)Input = Attributes[Index];
            Status = Ioctl(Irp, IOCTL_SMARTCARD_GET_ATTRIBUTE, Input, 4, Pass == 2 ? 1 : 64);
            Check("attribute %lx pass %lu", Attributes[Index], Pass);
        }
    }

    for (Pass = 0; Pass <= SCARD_SPECIFIC; Pass++)
    {
        static const ULONG StateAttributes[] =
        {
            SCARD_ATTR_ICC_PRESENCE, SCARD_ATTR_ICC_INTERFACE_STATUS, SCARD_ATTR_ATR_STRING,
            SCARD_ATTR_ICC_TYPE_PER_ATR, SCARD_ATTR_CURRENT_PROTOCOL_TYPE, SCARD_ATTR_CURRENT_D,
            SCARD_ATTR_CURRENT_BWT, SCARD_ATTR_CURRENT_CWT, SCARD_ATTR_CURRENT_W, SCARD_ATTR_CURRENT_IFSC,
        };

        for (Index = 0; Index < RTL_NUMBER_OF(StateAttributes); Index++)
        {
            Status = SetAtr(Index & 1 ? "3B808001*" : "3B90948171FE4501*", 2);
            ok_eq_hex(Status, STATUS_SUCCESS);
            SetCallbacks(TRUE);
            Extension.ReaderCapabilities.CurrentState = Pass;
            Extension.ReaderCapabilities.MechProperties = Pass & 1 ? SCARD_READER_SWALLOWS : 0;
            *(PULONG)Input = StateAttributes[Index];
            Status = Ioctl(Irp, IOCTL_SMARTCARD_GET_ATTRIBUTE, Input, 4, 64);
            Check("state %lu attribute %lx", Pass, StateAttributes[Index]);
        }
    }

    Status = SetAtr("3B021450", 0);
    Extension.ReaderCapabilities.CurrentState = SCARD_ABSENT;
    SetCallbacks(TRUE);
    CallbackStatus = STATUS_PENDING;
    CallbackAnswer = FALSE;
    KeepNotification = TRUE;
    Status = Ioctl(Irp, IOCTL_SMARTCARD_IS_PRESENT, NULL, 0, 0);
    Check("tracking pending");
    Status = Ioctl(SecondIrp, IOCTL_SMARTCARD_IS_PRESENT, NULL, 0, 0);
    Check("tracking second");
    Status = Ioctl(SecondIrp, IOCTL_SMARTCARD_IS_ABSENT, NULL, 0, 0);
    Check("tracking absent");
    Status = Ioctl(SecondIrp, IOCTL_SMARTCARD_GET_STATE, NULL, 0, 4);
    Check("tracking state");
    KeepNotification = FALSE;
    Extension.ReaderCapabilities.CurrentState = SCARD_PRESENT;
    CallbackStatus = STATUS_PENDING;
    Status = Ioctl(SecondIrp, IOCTL_SMARTCARD_IS_ABSENT, NULL, 0, 0);
    Check("tracking after");
    KeAcquireSpinLock(&Extension.OsData->SpinLock, &Irql);
    Extension.OsData->NotificationIrp = NULL;
    KeReleaseSpinLock(&Extension.OsData->SpinLock, Irql);

}

static
VOID
TestParse(VOID)
{
    static const PCSTR Atrs[] = { "3B00", "3B808001*", "3A00", "3B800100", "3B101A", "3B8002*", "3B" };
    ULONG Index, Pass;
    NTSTATUS Status;

    for (Pass = 0; Pass < 2; Pass++)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Atrs); Index++)
        {
            SetCallbacks(TRUE);
            CallbackStatus = STATUS_SUCCESS;
            ParseStatus = Pass == 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
            CallbackAnswer = FALSE;
            CallbackText[0] = ANSI_NULL;
            Status = SetAtr(Atrs[Index], 2);
            RtlStringCbPrintfA(Line, sizeof(Line), "st=%lx state=%lu sel=%lx sup=%lx |%s", Status,
                               Extension.ReaderCapabilities.CurrentState,
                               Extension.CardCapabilities.Protocol.Selected,
                               Extension.CardCapabilities.Protocol.Supported, CallbackText);
            Check("parse %lu %s", Pass, Atrs[Index]);
        }
    }
    ParseStatus = STATUS_SUCCESS;
}

static
VOID
TestIoctl(VOID)
{
    const IOCTL_CASE *Case;
    ULONG Index, Pass;
    UCHAR Input[16];
    NTSTATUS Status;
    PIRP Irp, SecondIrp;
    KIRQL Irql;

    Irp = IoAllocateIrp(2, FALSE);
    SecondIrp = IoAllocateIrp(2, FALSE);
    ok(Irp != NULL && SecondIrp != NULL, "No IRP\n");
    if (Irp == NULL || SecondIrp == NULL)
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(IoctlCases) + RTL_NUMBER_OF(LateCases); Index++)
    {
        if (Index == RTL_NUMBER_OF(IoctlCases))
        {
            TestIoctlStates(Irp, SecondIrp);
            TestParse();
        }
        Case = Index < RTL_NUMBER_OF(IoctlCases) ? &IoctlCases[Index] : &LateCases[Index - RTL_NUMBER_OF(IoctlCases)];
        Status = SetAtr(Case->Atr != NULL ? Case->Atr : "3B021450", Case->Variant);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Extension.ReaderCapabilities.CurrentState = Case->State;
        if (Case->Flags & IF_SELECTED_T0)
            Extension.CardCapabilities.Protocol.Selected = SCARD_PROTOCOL_T0;
        if (Case->Flags & IF_SELECTED_T1)
            Extension.CardCapabilities.Protocol.Selected = SCARD_PROTOCOL_T1;
        SetCallbacks(!(Case->Flags & IF_NO_CALLBACKS));
        CallbackStatus = Case->Answer;
        ParseStatus = (Case->Flags & IF_PARSE_FAILS) ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
        CallbackAnswer = (Case->Flags & IF_ANSWER) != 0;
        RtlZeroMemory(Input, sizeof(Input));
        *(PULONG)Input = Case->Input;
        if (Case->Code == IOCTL_SMARTCARD_TRANSMIT)
        {
            *(PULONG)(Input + 4) = sizeof(SCARD_IO_REQUEST);
            RtlCopyMemory(Input + 8, "\x00\xA4\x00\x00", 4);
        }
        Status = Ioctl(Irp, Case->Code, Input, Case->InputLength, Case->OutputLength);
        Check("ioctl %lu (%lx)", Index, Case->Code);
    }

    IoReuseIrp(SecondIrp, STATUS_SUCCESS);
    IoReuseIrp(Irp, STATUS_SUCCESS);
    IoFreeIrp(SecondIrp);
    IoFreeIrp(Irp);
}

static
VOID
TestInitialize(VOID)
{
    static const ULONG Versions[] = { 0, 0xFF, 0x100, 0x101, 0x14F, 0x150, 0x151, 0x200 };
    SMARTCARD_EXTENSION Local;
    UCHAR Data[8];
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Versions); Index++)
    {
        RtlZeroMemory(&Local, sizeof(Local));
        Local.Version = Versions[Index];
        Local.SmartcardRequest.BufferSize = Index == 5 ? 1000 : MIN_BUFFER_SIZE;
        Local.SmartcardReply.BufferSize = Index == 5 ? 600 : MIN_BUFFER_SIZE;
        Status = SmartcardInitialize(&Local);
        RtlStringCbPrintfA(Line, sizeof(Line), "v=%lx st=%lx req=%u/%lu/%lu rep=%u/%lu/%lu os=%u ver=%lx tables=%u/%u",
                           Versions[Index], Status, Local.SmartcardRequest.Buffer != NULL,
                           Local.SmartcardRequest.BufferSize, Local.SmartcardRequest.BufferLength,
                           Local.SmartcardReply.Buffer != NULL, Local.SmartcardReply.BufferSize,
                           Local.SmartcardReply.BufferLength, Local.OsData != NULL, Local.Version,
                           Local.CardCapabilities.ClockRateConversion != NULL,
                           Local.CardCapabilities.BitRateAdjustment != NULL);
        Check("initialize %lu", Index);
        if (NT_SUCCESS(Status))
        {
            if (Local.OsData != NULL)
            {
                RtlStringCbPrintfA(Line, sizeof(Line), "removed=%u ref=%ld dev=%p cur=%p not=%p list=%u",
                                   Local.OsData->RemoveLock.Removed, Local.OsData->RemoveLock.RefCount,
                                   Local.OsData->DeviceObject, Local.OsData->CurrentIrp, Local.OsData->NotificationIrp,
                                   IsListEmpty(&Local.OsData->RemoveLock.TagList));
                Check("osdata %lu", Index);
            }
            SmartcardExit(&Local);
            RtlStringCbPrintfA(Line, sizeof(Line), "exit req=%u rep=%u os=%u",
                               Local.SmartcardRequest.Buffer != NULL, Local.SmartcardReply.Buffer != NULL,
                               Local.OsData != NULL);
            Check("exit %lu", Index);
        }
    }

    if (!Start())
        return;

    PrintCard(0);
    Check("initial 0");
    PrintCard(1);
    Check("initial 1");
    PrintCard(2);
    Check("initial 2");
    if (Extension.CardCapabilities.ClockRateConversion != NULL && Extension.CardCapabilities.BitRateAdjustment != NULL)
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "F=%lu/%lu/%lu/%lu fs=%lu D=%lu/%lu/%lu/%lu",
                           Extension.CardCapabilities.ClockRateConversion[0].F,
                           Extension.CardCapabilities.ClockRateConversion[1].F,
                           Extension.CardCapabilities.ClockRateConversion[9].F,
                           Extension.CardCapabilities.ClockRateConversion[13].F,
                           Extension.CardCapabilities.ClockRateConversion[13].fs,
                           Extension.CardCapabilities.BitRateAdjustment[1].DNumerator,
                           Extension.CardCapabilities.BitRateAdjustment[8].DNumerator,
                           Extension.CardCapabilities.BitRateAdjustment[9].DNumerator,
                           Extension.CardCapabilities.BitRateAdjustment[9].DDivisor);
        Check("tables");
    }

    SetReader(4);
    SmartcardInitializeCardCapabilities(&Extension);
    PrintCard(0);
    Check("capabilities 0");
    PrintCard(1);
    Check("capabilities 1");
    PrintCard(2);
    Check("capabilities 2");
    SetReader(0);

    RtlCopyMemory(Data, "\x03\xFF\x00\x80\x01\xAA\x3F\x3B", 8);
    SmartcardInvertData(Data, 8);
    RtlStringCbCopyA(Line, sizeof(Line), Hex(0, Data, 8));
    Check("invert");

    RtlStringCbPrintfA(Line, sizeof(Line), "debug=%lx", SmartcardGetDebugLevel());
    Check("debug");

    Status = SmartcardAcquireRemoveLock(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "acquire st=%lx ref=%ld removed=%u", Status,
                       Extension.OsData->RemoveLock.RefCount, Extension.OsData->RemoveLock.Removed);
    Check("acquire");
    Status = SmartcardAcquireRemoveLockWithTag(&Extension, 'tseT');
    RtlStringCbPrintfA(Line, sizeof(Line), "acquire tag st=%lx ref=%ld list=%u", Status,
                       Extension.OsData->RemoveLock.RefCount, IsListEmpty(&Extension.OsData->RemoveLock.TagList));
    Check("acquire tag");
    SmartcardReleaseRemoveLockWithTag(&Extension, 'tseT');
    SmartcardReleaseRemoveLock(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "release ref=%ld list=%u", Extension.OsData->RemoveLock.RefCount,
                       IsListEmpty(&Extension.OsData->RemoveLock.TagList));
    Check("release");
}

static
VOID
TestRemove(VOID)
{
    NTSTATUS Status;

    Status = SmartcardAcquireRemoveLock(&Extension);
    ok_eq_hex(Status, STATUS_SUCCESS);
    SmartcardReleaseRemoveLockAndWait(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "wait ref=%ld removed=%u", Extension.OsData->RemoveLock.RefCount,
                       Extension.OsData->RemoveLock.Removed);
    Check("wait");
    Status = SmartcardAcquireRemoveLock(&Extension);
    RtlStringCbPrintfA(Line, sizeof(Line), "after st=%lx ref=%ld", Status, Extension.OsData->RemoveLock.RefCount);
    Check("after");
    SmartcardExit(&Extension);
}

START_TEST(SmcLib)
{
    NTSTATUS Status;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_SMARTCARD, 0, FALSE, &TestDevice);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    TestInitialize();
    if (Extension.OsData != NULL)
    {
        TestAtr();
        TestT0();
        TestT1();
        TestIoctl();
        SmartcardExit(&Extension);
    }
    IoDeleteDevice(TestDevice);
}

START_TEST(SmcLibRemove)
{
    NTSTATUS Status;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_SMARTCARD, 0, FALSE, &TestDevice);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    if (Start())
        TestRemove();
    IoDeleteDevice(TestDevice);
}
