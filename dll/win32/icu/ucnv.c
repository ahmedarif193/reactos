/*
 * PROJECT:     LiberNT International Components for Unicode
 * FILE:        dll/win32/icu/ucnv.c
 * PURPOSE:     ICU character set converters over Windows code pages
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "icu_private.h"
#include <string.h>

typedef enum _ICU_KIND
{
    IcuSingleByte,
    IcuDoubleByte,
    IcuUtf8
} ICU_KIND;

typedef struct _ICU_CODEPAGE
{
    const char *Name;
    UINT CodePage;
    const char *Mime;
    const char *Iana;
    const char *Aliases;
} ICU_CODEPAGE;

/* Aliases are NUL separated; names compare without case or punctuation. */
static const ICU_CODEPAGE IcuCodePages[] =
{
    { "UTF-8", CP_UTF8, "UTF-8", "UTF-8", "unicode-1-1-utf-8\0" },
    { "US-ASCII", 20127, "US-ASCII", "US-ASCII", "ascii\0ansi_x3.4-1968\0iso646-us\0us\0csascii\0" },
    { "ISO-8859-1", 28591, "ISO-8859-1", "ISO-8859-1", "latin1\0l1\0iso_8859-1:1987\0csisolatin1\0" },
    { "ISO-8859-2", 28592, "ISO-8859-2", "ISO-8859-2", "latin2\0l2\0iso_8859-2:1987\0csisolatin2\0" },
    { "ISO-8859-3", 28593, "ISO-8859-3", "ISO-8859-3", "latin3\0l3\0iso_8859-3:1988\0csisolatin3\0" },
    { "ISO-8859-4", 28594, "ISO-8859-4", "ISO-8859-4", "latin4\0l4\0iso_8859-4:1988\0csisolatin4\0" },
    { "ISO-8859-5", 28595, "ISO-8859-5", "ISO-8859-5", "cyrillic\0iso_8859-5:1988\0csisolatincyrillic\0" },
    { "ISO-8859-6", 28596, "ISO-8859-6", "ISO-8859-6", "arabic\0iso_8859-6:1987\0csisolatinarabic\0" },
    { "ISO-8859-7", 28597, "ISO-8859-7", "ISO-8859-7", "greek\0greek8\0iso_8859-7:1987\0csisolatingreek\0" },
    { "ISO-8859-8", 28598, "ISO-8859-8", "ISO-8859-8", "hebrew\0iso_8859-8:1988\0csisolatinhebrew\0" },
    { "ISO-8859-9", 28599, "ISO-8859-9", "ISO-8859-9", "latin5\0l5\0iso_8859-9:1989\0csisolatin5\0" },
    { "ISO-8859-13", 28603, NULL, "ISO-8859-13", "latin7\0" },
    { "ISO-8859-15", 28605, "ISO-8859-15", "ISO-8859-15", "latin9\0latin-9\0" },
    { "windows-1250", 1250, "windows-1250", "windows-1250", "" },
    { "windows-1251", 1251, "windows-1251", "windows-1251", "" },
    { "windows-1252", 1252, "windows-1252", "windows-1252", "" },
    { "windows-1253", 1253, "windows-1253", "windows-1253", "" },
    { "windows-1254", 1254, "windows-1254", "windows-1254", "" },
    { "windows-1255", 1255, "windows-1255", "windows-1255", "" },
    { "windows-1256", 1256, "windows-1256", "windows-1256", "" },
    { "windows-1257", 1257, "windows-1257", "windows-1257", "" },
    { "windows-1258", 1258, "windows-1258", "windows-1258", "" },
    { "windows-874", 874, NULL, "windows-874", "tis-620\0" },
    { "IBM437", 437, NULL, "IBM437", "cspc8codepage437\0" },
    { "IBM850", 850, NULL, "IBM850", "cspc850multilingual\0" },
    { "IBM852", 852, NULL, "IBM852", "cspcp852\0" },
    { "IBM855", 855, NULL, "IBM855", "csibm855\0" },
    { "IBM857", 857, NULL, "IBM857", "csibm857\0" },
    { "IBM860", 860, NULL, "IBM860", "csibm860\0" },
    { "IBM861", 861, NULL, "IBM861", "csibm861\0" },
    { "IBM862", 862, NULL, "IBM862", "cspc862latinhebrew\0" },
    { "IBM863", 863, NULL, "IBM863", "csibm863\0" },
    { "IBM864", 864, NULL, "IBM864", "csibm864\0" },
    { "IBM865", 865, NULL, "IBM865", "csibm865\0" },
    { "IBM866", 866, NULL, "IBM866", "csibm866\0" },
    { "IBM869", 869, NULL, "IBM869", "cp-gr\0csibm869\0" },
    { "KOI8-R", 20866, "KOI8-R", "KOI8-R", "cskoi8r\0" },
    { "KOI8-U", 21866, "KOI8-U", "KOI8-U", "" },
    { "macintosh", 10000, NULL, "macintosh", "mac\0csmacintosh\0macroman\0" },
    { "Shift_JIS", 932, "Shift_JIS", "Shift_JIS", "ms_kanji\0csshiftjis\0windows-31j\0cswindows31j\0sjis\0" },
    { "GBK", 936, "GBK", "GBK", "gb2312\0csgb2312\0" },
    { "windows-949", 949, NULL, "KS_C_5601-1987", "ks_c_5601-1989\0korean\0csksc56011987\0uhc\0" },
    { "Big5", 950, "Big5", "Big5", "csbig5\0x-big5\0" },
};

struct UConverter
{
    const ICU_CODEPAGE *Info;
    ICU_KIND Kind;
    int8_t MaxCharSize;
    int8_t SubCharLength;
    char SubChars[4];
    BYTE LeadBytes[32];
    BYTE SingleValid[32];
    WCHAR SingleChars[256];
    UConverterToUCallback ToUAction;
    const void *ToUContext;
    UConverterFromUCallback FromUAction;
    const void *FromUContext;
    int8_t ToULength;
    char ToUBytes[4];
    int32_t ToUOverflowLength;
    UChar ToUOverflow[32];
    UChar FromULead;
    int32_t FromUOverflowLength;
    char FromUOverflow[32];
};

#define ICU_NAME_MAX 64

#define IcuTestBit(Map, Byte) (((Map)[(Byte) >> 3] >> ((Byte) & 7)) & 1)
#define IcuSetBit(Map, Byte) ((Map)[(Byte) >> 3] |= (BYTE)(1 << ((Byte) & 7)))

#define IcuIsLead(c) (((c) & 0xFC00) == 0xD800)
#define IcuIsTrail(c) (((c) & 0xFC00) == 0xDC00)
#define IcuSupplementary(Lead, Trail) \
    ((((UChar32)(Lead) - 0xD800) << 10) + ((UChar32)(Trail) - 0xDC00) + 0x10000)

/* The sequence classification and decoding results besides a length. */
#define ICU_INCOMPLETE 0
#define ICU_UNASSIGNED 0
#define ICU_ILLEGAL (-1)
#define ICU_ILLEGAL_LEAD (-2)

static const ICU_CODEPAGE *IcuAvailable[sizeof(IcuCodePages) / sizeof(IcuCodePages[0])];
static int32_t IcuAvailableCount;

void U_EXPORT2 UCNV_TO_U_CALLBACK_SUBSTITUTE(const void *Context, UConverterToUnicodeArgs *Args,
                                            const char *CodeUnits, int32_t Length,
                                            UConverterCallbackReason Reason, UErrorCode *Err);
void U_EXPORT2 UCNV_FROM_U_CALLBACK_SUBSTITUTE(const void *Context, UConverterFromUnicodeArgs *Args,
                                              const UChar *CodeUnits, int32_t Length, UChar32 CodePoint,
                                              UConverterCallbackReason Reason, UErrorCode *Err);
void U_EXPORT2 UCNV_TO_U_CALLBACK_STOP(const void *Context, UConverterToUnicodeArgs *Args,
                                      const char *CodeUnits, int32_t Length,
                                      UConverterCallbackReason Reason, UErrorCode *Err);
void U_EXPORT2 UCNV_FROM_U_CALLBACK_STOP(const void *Context, UConverterFromUnicodeArgs *Args,
                                        const UChar *CodeUnits, int32_t Length, UChar32 CodePoint,
                                        UConverterCallbackReason Reason, UErrorCode *Err);

static BOOL IcuNormalizeName(const char *Name, char *Buffer)
{
    size_t Length = 0;

    for (; *Name; Name++)
    {
        char c = *Name;

        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if ((c < 'a' || c > 'z') && (c < '0' || c > '9'))
            continue;
        if (Length + 1 >= ICU_NAME_MAX)
            return FALSE;
        Buffer[Length++] = c;
    }
    Buffer[Length] = '\0';
    return TRUE;
}

static BOOL IcuNameMatches(const char *Normalized, const char *Candidate)
{
    char Buffer[ICU_NAME_MAX];

    return Candidate && IcuNormalizeName(Candidate, Buffer) && !strcmp(Normalized, Buffer);
}

static const ICU_CODEPAGE *IcuFindByNumber(UINT CodePage)
{
    int32_t i;

    for (i = 0; i < IcuAvailableCount; i++)
    {
        if (IcuAvailable[i]->CodePage == CodePage)
            return IcuAvailable[i];
    }
    return NULL;
}

static const ICU_CODEPAGE *IcuFindCodePage(const char *Name)
{
    static const char *const Prefixes[] = { "cp", "windows", "ibm", "ms", "" };
    char Normalized[ICU_NAME_MAX];
    const char *Alias;
    int32_t i;

    if (!IcuNormalizeName(Name, Normalized) || !Normalized[0])
        return NULL;

    for (i = 0; i < IcuAvailableCount; i++)
    {
        const ICU_CODEPAGE *Info = IcuAvailable[i];

        if (IcuNameMatches(Normalized, Info->Name) || IcuNameMatches(Normalized, Info->Mime) ||
            IcuNameMatches(Normalized, Info->Iana))
        {
            return Info;
        }
        for (Alias = Info->Aliases; *Alias; Alias += strlen(Alias) + 1)
        {
            if (IcuNameMatches(Normalized, Alias))
                return Info;
        }
    }

    /* Code page numbers: "cp1252", "windows-1252", "ibm-850", "ms936", "866". */
    for (i = 0; i < (int32_t)ARRAYSIZE(Prefixes); i++)
    {
        size_t Length = strlen(Prefixes[i]);
        const char *Digits = Normalized + Length;
        UINT CodePage = 0;

        if (strncmp(Normalized, Prefixes[i], Length) || !*Digits)
            continue;
        for (; *Digits >= '0' && *Digits <= '9' && CodePage < 100000; Digits++)
            CodePage = CodePage * 10 + (*Digits - '0');
        if (!*Digits)
            return IcuFindByNumber(CodePage);
    }
    return NULL;
}

static BOOL IcuInitializeConverter(UConverter *Cnv, const ICU_CODEPAGE *Info)
{
    CPINFOEXA CpInfo;
    UINT i;

    Cnv->Info = Info;
    Cnv->ToUAction = UCNV_TO_U_CALLBACK_SUBSTITUTE;
    Cnv->FromUAction = UCNV_FROM_U_CALLBACK_SUBSTITUTE;

    if (Info->CodePage == CP_UTF8)
    {
        Cnv->Kind = IcuUtf8;
        Cnv->MaxCharSize = 3;
        memcpy(Cnv->SubChars, "\xEF\xBF\xBD", 3);
        Cnv->SubCharLength = 3;
        return TRUE;
    }

    if (!GetCPInfoExA(Info->CodePage, 0, &CpInfo) || CpInfo.MaxCharSize < 1 || CpInfo.MaxCharSize > 2)
        return FALSE;

    Cnv->Kind = (CpInfo.MaxCharSize == 1) ? IcuSingleByte : IcuDoubleByte;
    Cnv->MaxCharSize = (int8_t)CpInfo.MaxCharSize;
    Cnv->SubChars[0] = (char)CpInfo.DefaultChar[0];
    Cnv->SubCharLength = 1;
    if (Cnv->Kind == IcuDoubleByte && CpInfo.DefaultChar[1])
    {
        Cnv->SubChars[1] = (char)CpInfo.DefaultChar[1];
        Cnv->SubCharLength = 2;
    }

    for (i = 0; i + 1 < MAX_LEADBYTES && CpInfo.LeadByte[i]; i += 2)
    {
        UINT Byte;

        for (Byte = CpInfo.LeadByte[i]; Byte <= CpInfo.LeadByte[i + 1] && Byte < 256; Byte++)
            IcuSetBit(Cnv->LeadBytes, Byte);
    }

    for (i = 0; i < 256; i++)
    {
        char Byte = (char)i;
        WCHAR Char;

        if (IcuTestBit(Cnv->LeadBytes, i) || (Info->CodePage == 20127 && i >= 0x80))
            continue;
        if (MultiByteToWideChar(Info->CodePage, MB_ERR_INVALID_CHARS, &Byte, 1, &Char, 1) == 1)
        {
            Cnv->SingleChars[i] = Char;
            IcuSetBit(Cnv->SingleValid, i);
        }
    }
    return TRUE;
}

/* Returns the length of the complete sequence starting the input, ICU_INCOMPLETE
 * when more bytes are needed, or minus the length of an illegal prefix. */
static int IcuSequenceLength(const UConverter *Cnv, const BYTE *Bytes, int Available)
{
    BYTE Lead = Bytes[0], Low = 0x80, High = 0xBF;
    int Length, i;

    if (Cnv->Kind == IcuSingleByte)
        return 1;
    if (Cnv->Kind == IcuDoubleByte)
    {
        if (!IcuTestBit(Cnv->LeadBytes, Lead))
            return 1;
        return (Available < 2) ? ICU_INCOMPLETE : 2;
    }

    if (Lead < 0x80)
        return 1;
    if (Lead < 0xC2 || Lead > 0xF4)
        return -1;
    Length = (Lead < 0xE0) ? 2 : ((Lead < 0xF0) ? 3 : 4);
    if (Lead == 0xE0)
        Low = 0xA0;
    else if (Lead == 0xED)
        High = 0x9F;
    else if (Lead == 0xF0)
        Low = 0x90;
    else if (Lead == 0xF4)
        High = 0x8F;
    for (i = 1; i < Length; i++)
    {
        if (i >= Available)
            return ICU_INCOMPLETE;
        if (Bytes[i] < Low || Bytes[i] > High)
            return -i;
        Low = 0x80;
        High = 0xBF;
    }
    return Length;
}

/* Returns the number of UChars produced, ICU_UNASSIGNED, ICU_ILLEGAL, or
 * ICU_ILLEGAL_LEAD when only the lead byte of a pair is illegal. */
static int IcuDecode(const UConverter *Cnv, const BYTE *Bytes, int Length, UChar *Chars)
{
    UChar32 Char;
    int Count;

    if (Cnv->Kind == IcuUtf8)
    {
        if (Length == 1)
            Char = Bytes[0];
        else if (Length == 2)
            Char = ((Bytes[0] & 0x1F) << 6) | (Bytes[1] & 0x3F);
        else if (Length == 3)
            Char = ((Bytes[0] & 0x0F) << 12) | ((Bytes[1] & 0x3F) << 6) | (Bytes[2] & 0x3F);
        else
            Char = ((Bytes[0] & 0x07) << 18) | ((Bytes[1] & 0x3F) << 12) | ((Bytes[2] & 0x3F) << 6) | (Bytes[3] & 0x3F);
        if (Char < 0x10000)
        {
            Chars[0] = (UChar)Char;
            return 1;
        }
        Char -= 0x10000;
        Chars[0] = (UChar)(0xD800 + (Char >> 10));
        Chars[1] = (UChar)(0xDC00 + (Char & 0x3FF));
        return 2;
    }

    if (Length == 1)
    {
        if (!IcuTestBit(Cnv->SingleValid, Bytes[0]))
            return (Cnv->Info->CodePage == 20127 && Bytes[0] >= 0x80) ? ICU_ILLEGAL : ICU_UNASSIGNED;
        Chars[0] = Cnv->SingleChars[Bytes[0]];
        return 1;
    }

    Count = MultiByteToWideChar(Cnv->Info->CodePage, MB_ERR_INVALID_CHARS, (LPCSTR)Bytes, 2, Chars, 2);
    if (Count > 0)
        return Count;
    return (Bytes[1] < 0x40) ? ICU_ILLEGAL_LEAD : ICU_UNASSIGNED;
}

/* Returns the byte count, or ICU_UNASSIGNED when the code page cannot represent the character. */
static int IcuEncode(const UConverter *Cnv, UChar32 Char, const UChar *Units, int32_t Count, char *Bytes)
{
    BOOL UsedDefault = FALSE;
    int Length;

    if (Cnv->Kind == IcuUtf8)
    {
        if (Char < 0x80)
        {
            Bytes[0] = (char)Char;
            return 1;
        }
        if (Char < 0x800)
        {
            Bytes[0] = (char)(0xC0 | (Char >> 6));
            Bytes[1] = (char)(0x80 | (Char & 0x3F));
            return 2;
        }
        if (Char < 0x10000)
        {
            if (Char >= 0xD800 && Char <= 0xDFFF)
                return ICU_UNASSIGNED;
            Bytes[0] = (char)(0xE0 | (Char >> 12));
            Bytes[1] = (char)(0x80 | ((Char >> 6) & 0x3F));
            Bytes[2] = (char)(0x80 | (Char & 0x3F));
            return 3;
        }
        Bytes[0] = (char)(0xF0 | (Char >> 18));
        Bytes[1] = (char)(0x80 | ((Char >> 12) & 0x3F));
        Bytes[2] = (char)(0x80 | ((Char >> 6) & 0x3F));
        Bytes[3] = (char)(0x80 | (Char & 0x3F));
        return 4;
    }

    Length = WideCharToMultiByte(Cnv->Info->CodePage, WC_NO_BEST_FIT_CHARS, Units, Count, Bytes, 4, NULL, &UsedDefault);
    return (Length > 0 && !UsedDefault) ? Length : ICU_UNASSIGNED;
}

/* Output that does not fit the target waits in the converter for the next call. */
static BOOL IcuPutToU(UConverter *Cnv, UConverterToUnicodeArgs *Args, const UChar *Chars, int32_t Count,
                      int32_t Offset)
{
    int32_t i = 0;

    if (!Cnv->ToUOverflowLength)
    {
        for (; i < Count && Args->target < Args->targetLimit; i++)
        {
            *Args->target++ = Chars[i];
            if (Args->offsets)
                *Args->offsets++ = Offset;
        }
    }
    for (; i < Count && Cnv->ToUOverflowLength < (int32_t)ARRAYSIZE(Cnv->ToUOverflow); i++)
        Cnv->ToUOverflow[Cnv->ToUOverflowLength++] = Chars[i];
    return !Cnv->ToUOverflowLength;
}

static BOOL IcuPutFromU(UConverter *Cnv, UConverterFromUnicodeArgs *Args, const char *Bytes, int32_t Length,
                        int32_t Offset)
{
    int32_t i = 0;

    if (!Cnv->FromUOverflowLength)
    {
        for (; i < Length && Args->target < Args->targetLimit; i++)
        {
            *Args->target++ = Bytes[i];
            if (Args->offsets)
                *Args->offsets++ = Offset;
        }
    }
    for (; i < Length && Cnv->FromUOverflowLength < (int32_t)ARRAYSIZE(Cnv->FromUOverflow); i++)
        Cnv->FromUOverflow[Cnv->FromUOverflowLength++] = Bytes[i];
    return !Cnv->FromUOverflowLength;
}

static BOOL IcuFlushToU(UConverter *Cnv, UConverterToUnicodeArgs *Args, UErrorCode *Err)
{
    int32_t i = 0;

    for (; i < Cnv->ToUOverflowLength && Args->target < Args->targetLimit; i++)
    {
        *Args->target++ = Cnv->ToUOverflow[i];
        if (Args->offsets)
            *Args->offsets++ = -1;
    }
    Cnv->ToUOverflowLength -= i;
    memmove(Cnv->ToUOverflow, Cnv->ToUOverflow + i, Cnv->ToUOverflowLength * sizeof(UChar));
    if (Cnv->ToUOverflowLength)
    {
        *Err = U_BUFFER_OVERFLOW_ERROR;
        return FALSE;
    }
    return TRUE;
}

static BOOL IcuFlushFromU(UConverter *Cnv, UConverterFromUnicodeArgs *Args, UErrorCode *Err)
{
    int32_t i = 0;

    for (; i < Cnv->FromUOverflowLength && Args->target < Args->targetLimit; i++)
    {
        *Args->target++ = Cnv->FromUOverflow[i];
        if (Args->offsets)
            *Args->offsets++ = -1;
    }
    Cnv->FromUOverflowLength -= i;
    memmove(Cnv->FromUOverflow, Cnv->FromUOverflow + i, Cnv->FromUOverflowLength);
    if (Cnv->FromUOverflowLength)
    {
        *Err = U_BUFFER_OVERFLOW_ERROR;
        return FALSE;
    }
    return TRUE;
}

static BOOL IcuToUCallback(UConverter *Cnv, UConverterToUnicodeArgs *Args, const BYTE *Bytes, int32_t Length,
                           UConverterCallbackReason Reason, UErrorCode Error, UErrorCode *Err)
{
    *Err = Error;
    Cnv->ToUAction(Cnv->ToUContext, Args, (const char *)Bytes, Length, Reason, Err);
    return U_SUCCESS(*Err);
}

static BOOL IcuFromUCallback(UConverter *Cnv, UConverterFromUnicodeArgs *Args, const UChar *Units,
                             int32_t Count, UChar32 Char, UConverterCallbackReason Reason, UErrorCode Error,
                             UErrorCode *Err)
{
    *Err = Error;
    Cnv->FromUAction(Cnv->FromUContext, Args, Units, Count, Char, Reason, Err);
    return U_SUCCESS(*Err);
}

/* Consumes input from the bytes left over by the previous call first. */
static void IcuConsumeToU(UConverter *Cnv, UConverterToUnicodeArgs *Args, int Length)
{
    int Pending = Cnv->ToULength;

    if (Length >= Pending)
    {
        Args->source += Length - Pending;
        Cnv->ToULength = 0;
    }
    else
    {
        memmove(Cnv->ToUBytes, Cnv->ToUBytes + Length, Pending - Length);
        Cnv->ToULength = (int8_t)(Pending - Length);
    }
}

static void IcuToUnicode(UConverter *Cnv, UConverterToUnicodeArgs *Args, UErrorCode *Err)
{
    const char *SourceStart = Args->source;

    if (!IcuFlushToU(Cnv, Args, Err))
        return;

    for (;;)
    {
        int Pending = Cnv->ToULength, Available = Pending, Length, Count;
        int32_t Offset = Pending ? -1 : (int32_t)(Args->source - SourceStart);
        BYTE Bytes[4];
        UChar Chars[2];

        memcpy(Bytes, Cnv->ToUBytes, Pending);
        while (Available < 4 && Args->source + (Available - Pending) < Args->sourceLimit)
        {
            Bytes[Available] = (BYTE)Args->source[Available - Pending];
            Available++;
        }
        if (!Available)
            break;

        Length = IcuSequenceLength(Cnv, Bytes, Available);
        if (Length == ICU_INCOMPLETE)
        {
            memcpy(Cnv->ToUBytes, Bytes, Available);
            Cnv->ToULength = (int8_t)Available;
            Args->source = Args->sourceLimit;
            if (!Args->flush)
                break;
            Cnv->ToULength = 0;
            if (!IcuToUCallback(Cnv, Args, Bytes, Available, UCNV_ILLEGAL, U_TRUNCATED_CHAR_FOUND, Err))
                return;
            continue;
        }

        if (Args->target >= Args->targetLimit)
        {
            *Err = U_BUFFER_OVERFLOW_ERROR;
            return;
        }

        if (Length < 0)
        {
            IcuConsumeToU(Cnv, Args, -Length);
            if (!IcuToUCallback(Cnv, Args, Bytes, -Length, UCNV_ILLEGAL, U_ILLEGAL_CHAR_FOUND, Err))
                return;
            continue;
        }

        Count = IcuDecode(Cnv, Bytes, Length, Chars);
        if (Count == ICU_ILLEGAL_LEAD)
        {
            IcuConsumeToU(Cnv, Args, 1);
            if (!IcuToUCallback(Cnv, Args, Bytes, 1, UCNV_ILLEGAL, U_ILLEGAL_CHAR_FOUND, Err))
                return;
            continue;
        }

        IcuConsumeToU(Cnv, Args, Length);
        if (Count == ICU_ILLEGAL)
        {
            if (!IcuToUCallback(Cnv, Args, Bytes, Length, UCNV_ILLEGAL, U_ILLEGAL_CHAR_FOUND, Err))
                return;
        }
        else if (Count == ICU_UNASSIGNED)
        {
            if (!IcuToUCallback(Cnv, Args, Bytes, Length, UCNV_UNASSIGNED, U_INVALID_CHAR_FOUND, Err))
                return;
        }
        else if (!IcuPutToU(Cnv, Args, Chars, Count, Offset))
        {
            *Err = U_BUFFER_OVERFLOW_ERROR;
            return;
        }
    }
}

static void IcuFromUnicode(UConverter *Cnv, UConverterFromUnicodeArgs *Args, UErrorCode *Err)
{
    const UChar *SourceStart = Args->source;

    if (!IcuFlushFromU(Cnv, Args, Err))
        return;

    for (;;)
    {
        UChar Units[2];
        int32_t Count = 1, Offset = -1;
        UChar32 Char;
        char Bytes[8];
        int Length;

        if (Cnv->FromULead)
        {
            Units[0] = Cnv->FromULead;
            if (Args->source >= Args->sourceLimit)
            {
                if (!Args->flush)
                    break;
                Cnv->FromULead = 0;
                if (!IcuFromUCallback(Cnv, Args, Units, 1, Units[0], UCNV_ILLEGAL, U_TRUNCATED_CHAR_FOUND, Err))
                    return;
                continue;
            }
            if (Args->target >= Args->targetLimit)
            {
                *Err = U_BUFFER_OVERFLOW_ERROR;
                return;
            }
            Cnv->FromULead = 0;
            if (!IcuIsTrail(*Args->source))
            {
                if (!IcuFromUCallback(Cnv, Args, Units, 1, Units[0], UCNV_ILLEGAL, U_ILLEGAL_CHAR_FOUND, Err))
                    return;
                continue;
            }
            Units[1] = *Args->source++;
            Count = 2;
        }
        else
        {
            if (Args->source >= Args->sourceLimit)
                break;
            if (Args->target >= Args->targetLimit)
            {
                *Err = U_BUFFER_OVERFLOW_ERROR;
                return;
            }
            Offset = (int32_t)(Args->source - SourceStart);
            Units[0] = *Args->source++;
            if (IcuIsLead(Units[0]))
            {
                if (Args->source >= Args->sourceLimit)
                {
                    Cnv->FromULead = Units[0];
                    continue;
                }
                if (!IcuIsTrail(*Args->source))
                {
                    if (!IcuFromUCallback(Cnv, Args, Units, 1, Units[0], UCNV_ILLEGAL, U_ILLEGAL_CHAR_FOUND, Err))
                        return;
                    continue;
                }
                Units[1] = *Args->source++;
                Count = 2;
            }
            else if (IcuIsTrail(Units[0]))
            {
                if (!IcuFromUCallback(Cnv, Args, Units, 1, Units[0], UCNV_ILLEGAL, U_ILLEGAL_CHAR_FOUND, Err))
                    return;
                continue;
            }
        }

        Char = (Count == 2) ? IcuSupplementary(Units[0], Units[1]) : Units[0];
        Length = IcuEncode(Cnv, Char, Units, Count, Bytes);
        if (Length == ICU_UNASSIGNED)
        {
            if (!IcuFromUCallback(Cnv, Args, Units, Count, Char, UCNV_UNASSIGNED, U_INVALID_CHAR_FOUND, Err))
                return;
        }
        else if (!IcuPutFromU(Cnv, Args, Bytes, Length, Offset))
        {
            *Err = U_BUFFER_OVERFLOW_ERROR;
            return;
        }
    }
}

static void IcuNotifyCallbacks(UConverter *Cnv, UConverterCallbackReason Reason)
{
    UConverterToUnicodeArgs ToUArgs = { sizeof(ToUArgs), TRUE, Cnv, NULL, NULL, NULL, NULL, NULL };
    UConverterFromUnicodeArgs FromUArgs = { sizeof(FromUArgs), TRUE, Cnv, NULL, NULL, NULL, NULL, NULL };
    UErrorCode Error = U_ZERO_ERROR;

    if (Cnv->ToUAction != UCNV_TO_U_CALLBACK_SUBSTITUTE)
        Cnv->ToUAction(Cnv->ToUContext, &ToUArgs, NULL, 0, Reason, &Error);
    Error = U_ZERO_ERROR;
    if (Cnv->FromUAction != UCNV_FROM_U_CALLBACK_SUBSTITUTE)
        Cnv->FromUAction(Cnv->FromUContext, &FromUArgs, NULL, 0, 0, Reason, &Error);
}

static BOOL IcuStopsOnReason(const void *Context, UConverterCallbackReason Reason)
{
    return Context && !(*(const char *)Context == UCNV_SUB_STOP_ON_ILLEGAL[0] && Reason == UCNV_UNASSIGNED);
}

UConverter * U_EXPORT2 ucnv_open(const char *Name, UErrorCode *Err)
{
    const ICU_CODEPAGE *Info;
    UConverter *Cnv;

    if (!Err || U_FAILURE(*Err))
        return NULL;

    Info = (Name && *Name) ? IcuFindCodePage(Name) : IcuFindByNumber(GetACP());
    if (!Info)
    {
        *Err = U_FILE_ACCESS_ERROR;
        return NULL;
    }

    Cnv = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Cnv));
    if (!Cnv)
    {
        *Err = U_MEMORY_ALLOCATION_ERROR;
        return NULL;
    }
    if (!IcuInitializeConverter(Cnv, Info))
    {
        HeapFree(GetProcessHeap(), 0, Cnv);
        *Err = U_FILE_ACCESS_ERROR;
        return NULL;
    }
    return Cnv;
}

void U_EXPORT2 ucnv_close(UConverter *Cnv)
{
    if (!Cnv)
        return;
    IcuNotifyCallbacks(Cnv, UCNV_CLOSE);
    HeapFree(GetProcessHeap(), 0, Cnv);
}

void U_EXPORT2 ucnv_reset(UConverter *Cnv)
{
    if (!Cnv)
        return;
    IcuNotifyCallbacks(Cnv, UCNV_RESET);
    Cnv->ToULength = 0;
    Cnv->ToUOverflowLength = 0;
    Cnv->FromULead = 0;
    Cnv->FromUOverflowLength = 0;
}

int8_t U_EXPORT2 ucnv_getMaxCharSize(const UConverter *Cnv)
{
    return Cnv ? Cnv->MaxCharSize : 0;
}

const char * U_EXPORT2 ucnv_getName(const UConverter *Cnv, UErrorCode *Err)
{
    if (!Err || U_FAILURE(*Err))
        return NULL;
    if (!Cnv)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }
    return Cnv->Info->Name;
}

const char * U_EXPORT2 ucnv_getStandardName(const char *Name, const char *Standard, UErrorCode *Err)
{
    char Normalized[ICU_NAME_MAX];
    const ICU_CODEPAGE *Info;

    if (!Err || U_FAILURE(*Err))
        return NULL;
    if (!Name || !Standard)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }

    Info = IcuFindCodePage(Name);
    if (!Info || !IcuNormalizeName(Standard, Normalized))
        return NULL;
    if (!strcmp(Normalized, "mime"))
        return Info->Mime;
    if (!strcmp(Normalized, "iana"))
        return Info->Iana;
    return NULL;
}

int32_t U_EXPORT2 ucnv_countAvailable(void)
{
    return IcuAvailableCount;
}

const char * U_EXPORT2 ucnv_getAvailableName(int32_t Index)
{
    return (Index >= 0 && Index < IcuAvailableCount) ? IcuAvailable[Index]->Name : NULL;
}

void U_EXPORT2 ucnv_setToUCallBack(UConverter *Cnv, UConverterToUCallback NewAction, const void *NewContext,
                                   UConverterToUCallback *OldAction, const void **OldContext, UErrorCode *Err)
{
    if (!Err || U_FAILURE(*Err))
        return;
    if (!Cnv)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if (OldAction)
        *OldAction = Cnv->ToUAction;
    if (OldContext)
        *OldContext = Cnv->ToUContext;
    Cnv->ToUAction = NewAction ? NewAction : UCNV_TO_U_CALLBACK_STOP;
    Cnv->ToUContext = NewContext;
}

void U_EXPORT2 ucnv_setFromUCallBack(UConverter *Cnv, UConverterFromUCallback NewAction, const void *NewContext,
                                     UConverterFromUCallback *OldAction, const void **OldContext, UErrorCode *Err)
{
    if (!Err || U_FAILURE(*Err))
        return;
    if (!Cnv)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if (OldAction)
        *OldAction = Cnv->FromUAction;
    if (OldContext)
        *OldContext = Cnv->FromUContext;
    Cnv->FromUAction = NewAction ? NewAction : UCNV_FROM_U_CALLBACK_STOP;
    Cnv->FromUContext = NewContext;
}

void U_EXPORT2 ucnv_getToUCallBack(const UConverter *Cnv, UConverterToUCallback *Action, const void **Context)
{
    if (!Cnv || !Action || !Context)
        return;
    *Action = Cnv->ToUAction;
    *Context = Cnv->ToUContext;
}

void U_EXPORT2 ucnv_getFromUCallBack(const UConverter *Cnv, UConverterFromUCallback *Action, const void **Context)
{
    if (!Cnv || !Action || !Context)
        return;
    *Action = Cnv->FromUAction;
    *Context = Cnv->FromUContext;
}

void U_EXPORT2 ucnv_toUnicode(UConverter *Cnv, UChar **Target, const UChar *TargetLimit, const char **Source,
                              const char *SourceLimit, int32_t *Offsets, UBool Flush, UErrorCode *Err)
{
    UConverterToUnicodeArgs Args;

    if (!Err || U_FAILURE(*Err))
        return;
    if (!Cnv || !Target || !Source || TargetLimit < *Target || SourceLimit < *Source)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    Args.size = sizeof(Args);
    Args.flush = Flush;
    Args.converter = Cnv;
    Args.source = *Source;
    Args.sourceLimit = SourceLimit;
    Args.target = *Target;
    Args.targetLimit = TargetLimit;
    Args.offsets = Offsets;
    IcuToUnicode(Cnv, &Args, Err);
    *Source = Args.source;
    *Target = Args.target;
}

void U_EXPORT2 ucnv_fromUnicode(UConverter *Cnv, char **Target, const char *TargetLimit, const UChar **Source,
                                const UChar *SourceLimit, int32_t *Offsets, UBool Flush, UErrorCode *Err)
{
    UConverterFromUnicodeArgs Args;

    if (!Err || U_FAILURE(*Err))
        return;
    if (!Cnv || !Target || !Source || TargetLimit < *Target || SourceLimit < *Source)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    Args.size = sizeof(Args);
    Args.flush = Flush;
    Args.converter = Cnv;
    Args.source = *Source;
    Args.sourceLimit = SourceLimit;
    Args.target = *Target;
    Args.targetLimit = TargetLimit;
    Args.offsets = Offsets;
    IcuFromUnicode(Cnv, &Args, Err);
    *Source = Args.source;
    *Target = Args.target;
}

int32_t U_EXPORT2 ucnv_toUCountPending(const UConverter *Cnv, UErrorCode *Err)
{
    if (!Err || U_FAILURE(*Err))
        return -1;
    if (!Cnv)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }
    return Cnv->ToULength;
}

int32_t U_EXPORT2 ucnv_fromUCountPending(const UConverter *Cnv, UErrorCode *Err)
{
    if (!Err || U_FAILURE(*Err))
        return -1;
    if (!Cnv)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }
    return Cnv->FromULead ? 1 : 0;
}

void U_EXPORT2 ucnv_cbToUWriteUChars(UConverterToUnicodeArgs *Args, const UChar *Source, int32_t Length,
                                     int32_t OffsetIndex, UErrorCode *Err)
{
    if (!Err || U_FAILURE(*Err))
        return;
    if (!Args || !Args->converter || Length < 0 || (!Source && Length))
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if (!IcuPutToU(Args->converter, Args, Source, Length, OffsetIndex))
        *Err = U_BUFFER_OVERFLOW_ERROR;
}

void U_EXPORT2 ucnv_cbFromUWriteUChars(UConverterFromUnicodeArgs *Args, const UChar **Source,
                                       const UChar *SourceLimit, int32_t OffsetIndex, UErrorCode *Err)
{
    UConverter *Cnv;

    if (!Err || U_FAILURE(*Err))
        return;
    if (!Args || !Args->converter || !Source || !*Source || SourceLimit < *Source)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    Cnv = Args->converter;
    while (*Source < SourceLimit)
    {
        UChar Units[2];
        int32_t Count = 1;
        char Bytes[8];
        int Length;

        Units[0] = *(*Source)++;
        if (IcuIsLead(Units[0]) && *Source < SourceLimit && IcuIsTrail(**Source))
        {
            Units[1] = *(*Source)++;
            Count = 2;
        }
        Length = IcuEncode(Cnv, (Count == 2) ? IcuSupplementary(Units[0], Units[1]) : Units[0],
                           Units, Count, Bytes);
        if (Length == ICU_UNASSIGNED)
        {
            memcpy(Bytes, Cnv->SubChars, Cnv->SubCharLength);
            Length = Cnv->SubCharLength;
        }
        if (!IcuPutFromU(Cnv, Args, Bytes, Length, OffsetIndex))
            *Err = U_BUFFER_OVERFLOW_ERROR;
    }
}

void U_EXPORT2 UCNV_TO_U_CALLBACK_STOP(const void *Context, UConverterToUnicodeArgs *Args,
                                      const char *CodeUnits, int32_t Length,
                                      UConverterCallbackReason Reason, UErrorCode *Err)
{
}

void U_EXPORT2 UCNV_FROM_U_CALLBACK_STOP(const void *Context, UConverterFromUnicodeArgs *Args,
                                        const UChar *CodeUnits, int32_t Length, UChar32 CodePoint,
                                        UConverterCallbackReason Reason, UErrorCode *Err)
{
}

void U_EXPORT2 UCNV_TO_U_CALLBACK_SKIP(const void *Context, UConverterToUnicodeArgs *Args,
                                      const char *CodeUnits, int32_t Length,
                                      UConverterCallbackReason Reason, UErrorCode *Err)
{
    if (Reason <= UCNV_IRREGULAR && !IcuStopsOnReason(Context, Reason))
        *Err = U_ZERO_ERROR;
}

void U_EXPORT2 UCNV_FROM_U_CALLBACK_SKIP(const void *Context, UConverterFromUnicodeArgs *Args,
                                        const UChar *CodeUnits, int32_t Length, UChar32 CodePoint,
                                        UConverterCallbackReason Reason, UErrorCode *Err)
{
    if (Reason <= UCNV_IRREGULAR && !IcuStopsOnReason(Context, Reason))
        *Err = U_ZERO_ERROR;
}

void U_EXPORT2 UCNV_TO_U_CALLBACK_SUBSTITUTE(const void *Context, UConverterToUnicodeArgs *Args,
                                            const char *CodeUnits, int32_t Length,
                                            UConverterCallbackReason Reason, UErrorCode *Err)
{
    static const UChar Replacement = 0xFFFD;

    if (Reason > UCNV_IRREGULAR || IcuStopsOnReason(Context, Reason))
        return;
    *Err = U_ZERO_ERROR;
    ucnv_cbToUWriteUChars(Args, &Replacement, 1, 0, Err);
}

void U_EXPORT2 UCNV_FROM_U_CALLBACK_SUBSTITUTE(const void *Context, UConverterFromUnicodeArgs *Args,
                                              const UChar *CodeUnits, int32_t Length, UChar32 CodePoint,
                                              UConverterCallbackReason Reason, UErrorCode *Err)
{
    UConverter *Cnv;

    if (Reason > UCNV_IRREGULAR || IcuStopsOnReason(Context, Reason))
        return;
    *Err = U_ZERO_ERROR;
    if (!Args || !Args->converter)
    {
        *Err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    Cnv = Args->converter;
    if (!IcuPutFromU(Cnv, Args, Cnv->SubChars, Cnv->SubCharLength, 0))
        *Err = U_BUFFER_OVERFLOW_ERROR;
}

const char * U_EXPORT2 u_errorName(UErrorCode Code)
{
    static const char *const Errors[] =
    {
        "U_ZERO_ERROR", "U_ILLEGAL_ARGUMENT_ERROR", "U_MISSING_RESOURCE_ERROR", "U_INVALID_FORMAT_ERROR",
        "U_FILE_ACCESS_ERROR", "U_INTERNAL_PROGRAM_ERROR", "U_MESSAGE_PARSE_ERROR", "U_MEMORY_ALLOCATION_ERROR",
        "U_INDEX_OUTOFBOUNDS_ERROR", "U_PARSE_ERROR", "U_INVALID_CHAR_FOUND", "U_TRUNCATED_CHAR_FOUND",
        "U_ILLEGAL_CHAR_FOUND", "U_INVALID_TABLE_FORMAT", "U_INVALID_TABLE_FILE", "U_BUFFER_OVERFLOW_ERROR",
        "U_UNSUPPORTED_ERROR"
    };

    if (Code == U_USING_DEFAULT_WARNING)
        return "U_USING_DEFAULT_WARNING";
    if (Code == U_AMBIGUOUS_ALIAS_WARNING)
        return "U_AMBIGUOUS_ALIAS_WARNING";
    if (Code >= U_ZERO_ERROR && Code < (int32_t)ARRAYSIZE(Errors))
        return Errors[Code];
    return "[BOGUS UErrorCode]";
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    int32_t i;

    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        for (i = 0; i < (int32_t)ARRAYSIZE(IcuCodePages); i++)
        {
            if (IsValidCodePage(IcuCodePages[i].CodePage))
                IcuAvailable[IcuAvailableCount++] = &IcuCodePages[i];
        }
    }
    return TRUE;
}
