/*
 * PROJECT:     ReactOS Code Integrity
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Bounded PE Authenticode and RFC 3161 verification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 *
 * Formats: PE/COFF Authenticode, RFC 5652 SignedData and RFC 3161 TSTInfo.
 * RSA and certificate-chain validation are provided by Mbed TLS.
 */

#include <reactos/ci.h>
#include <string.h>
#include <mbedtls/md.h>
#include <mbedtls/oid.h>
#include <mbedtls/x509_crt.h>
#include "roots.h"

#define CI_MAX_CERTIFICATES 16
#define CI_MAX_CERTIFICATE_BYTES (1024 * 1024)
#define CI_HASH_BUFFER_SIZE 65536
#define CI_OID_EQUAL(Value, Oid) ((Value).Tag == 6 && (Value).Size == sizeof(Oid) - 1 && !memcmp((Value).Data, Oid, sizeof(Oid) - 1))

static const char CiSignedDataOid[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x07\x02";
static const char CiSpcIndirectDataOid[] = "\x2b\x06\x01\x04\x01\x82\x37\x02\x01\x04";
static const char CiSpcPeImageOid[] = "\x2b\x06\x01\x04\x01\x82\x37\x02\x01\x0f";
static const char CiContentTypeOid[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x09\x03";
static const char CiMessageDigestOid[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x09\x04";
static const char CiTimestampOid[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x09\x10\x02\x0e";
static const char CiMicrosoftTimestampOid[] = "\x2b\x06\x01\x04\x01\x82\x37\x03\x03\x01";
static const char CiTstInfoOid[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x09\x10\x01\x04";

typedef struct _CI_DER
{
    const unsigned char *Start;
    const unsigned char *Data;
    size_t Size;
    unsigned int Tag;
} CI_DER;

typedef struct _CI_CURSOR
{
    const unsigned char *Data;
    size_t Size;
} CI_CURSOR;

typedef struct _CI_SIGNED_DATA
{
    CI_DER ContentType;
    CI_DER Content;
    CI_DER Certificates[CI_MAX_CERTIFICATES];
    unsigned int CertificateCount;
    CI_DER Issuer;
    CI_DER Serial;
    CI_DER Attributes;
    CI_DER Signature;
    CI_DER ExtraAttributes;
    mbedtls_md_type_t DigestType;
} CI_SIGNED_DATA;

static CI_CURSOR
CiContents(CI_DER Value)
{
    CI_CURSOR Cursor = {Value.Data, Value.Size};
    return Cursor;
}

static size_t
CiEncodedSize(CI_DER Value)
{
    return (size_t)(Value.Data - Value.Start) + Value.Size;
}

static int
CiReadDer(CI_CURSOR *Cursor, unsigned int Tag, CI_DER *Value)
{
    size_t Length, Header = 2, Count, Index;

    if (Cursor->Size < 2 || (Cursor->Data[0] & 31) == 31 || Cursor->Data[0] != Tag)
        return 0;
    Length = Cursor->Data[1];
    if (Length & 0x80)
    {
        Count = Length & 0x7f;
        if (!Count || Count > sizeof(size_t) || Count > Cursor->Size - Header || !Cursor->Data[2])
            return 0;
        Length = 0;
        for (Index = 0; Index < Count; ++Index)
            Length = (Length << 8) | Cursor->Data[Header++];
        if (Length < 128)
            return 0;
    }
    if (Length > Cursor->Size - Header)
        return 0;
    Value->Start = Cursor->Data;
    Value->Data = Cursor->Data + Header;
    Value->Size = Length;
    Value->Tag = Tag;
    Cursor->Data += Header + Length;
    Cursor->Size -= Header + Length;
    return 1;
}

static int
CiEqual(CI_DER Left, CI_DER Right)
{
    return Left.Tag == Right.Tag && Left.Size == Right.Size && !memcmp(Left.Data, Right.Data, Left.Size);
}

static int
CiReadAlgorithm(CI_CURSOR *Cursor, mbedtls_md_type_t *Type)
{
    CI_DER Sequence, Oid, Null;
    CI_CURSOR Algorithm;
    mbedtls_asn1_buf Buffer;

    if (!CiReadDer(Cursor, 0x30, &Sequence))
        return 0;
    Algorithm = CiContents(Sequence);
    if (!CiReadDer(&Algorithm, 6, &Oid))
        return 0;
    if (Algorithm.Size && (!CiReadDer(&Algorithm, 5, &Null) || Null.Size || Algorithm.Size))
        return 0;
    Buffer.tag = 6;
    Buffer.p = (unsigned char *)Oid.Data;
    Buffer.len = Oid.Size;
    if (mbedtls_oid_get_md_alg(&Buffer, Type))
        return 0;
    return *Type == MBEDTLS_MD_SHA256 || *Type == MBEDTLS_MD_SHA384 || *Type == MBEDTLS_MD_SHA512;
}

static int
CiParseSignedData(CI_DER Input, int Timestamp, CI_SIGNED_DATA *Signed)
{
    CI_CURSOR Cursor = CiContents(Input), Inner, Fields;
    CI_DER Value, Oid, Wrapped, Sequence, Version, Signer;
    mbedtls_md_type_t SignerDigest;

    memset(Signed, 0, sizeof(*Signed));
    if (!CiReadDer(&Cursor, 6, &Oid) || !CI_OID_EQUAL(Oid, CiSignedDataOid) || !CiReadDer(&Cursor, 0xa0, &Wrapped) || Cursor.Size)
        return 0;
    Cursor = CiContents(Wrapped);
    if (!CiReadDer(&Cursor, 0x30, &Sequence) || Cursor.Size)
        return 0;
    Cursor = CiContents(Sequence);
    if (!CiReadDer(&Cursor, 2, &Version) || Version.Size != 1 || Version.Data[0] != (Timestamp ? 3 : 1))
        return 0;
    if (!CiReadDer(&Cursor, 0x31, &Value))
        return 0;
    Inner = CiContents(Value);
    if (!CiReadAlgorithm(&Inner, &Signed->DigestType) || Inner.Size)
        return 0;
    if (!CiReadDer(&Cursor, 0x30, &Sequence))
        return 0;
    Inner = CiContents(Sequence);
    if (!CiReadDer(&Inner, 6, &Signed->ContentType) || !CiReadDer(&Inner, 0xa0, &Wrapped) || Inner.Size)
        return 0;
    if (Timestamp ? !CI_OID_EQUAL(Signed->ContentType, CiTstInfoOid) : !CI_OID_EQUAL(Signed->ContentType, CiSpcIndirectDataOid))
        return 0;
    Inner = CiContents(Wrapped);
    if (!CiReadDer(&Inner, Timestamp ? 4 : 0x30, &Signed->Content) || Inner.Size)
        return 0;
    if (!CiReadDer(&Cursor, 0xa0, &Value))
        return 0;
    Inner = CiContents(Value);
    while (Inner.Size)
    {
        /* Attribute certificates are not public-key certificates or trust anchors. */
        if (Inner.Data[0] == 0xa1 || Inner.Data[0] == 0xa2)
        {
            if (!CiReadDer(&Inner, Inner.Data[0], &Value))
                return 0;
            continue;
        }
        if (Signed->CertificateCount == CI_MAX_CERTIFICATES || !CiReadDer(&Inner, 0x30, &Signed->Certificates[Signed->CertificateCount]))
            return 0;
        ++Signed->CertificateCount;
    }
    if (!Signed->CertificateCount)
        return 0;
    if (Cursor.Size && Cursor.Data[0] == 0xa1 && !CiReadDer(&Cursor, 0xa1, &Value))
        return 0;
    if (!CiReadDer(&Cursor, 0x31, &Value) || Cursor.Size)
        return 0;
    Inner = CiContents(Value);
    if (!CiReadDer(&Inner, 0x30, &Signer) || Inner.Size)
        return 0;
    Inner = CiContents(Signer);
    if (!CiReadDer(&Inner, 2, &Version) || Version.Size != 1 || Version.Data[0] != 1 || !CiReadDer(&Inner, 0x30, &Value))
        return 0;
    Fields = CiContents(Value);
    if (!CiReadDer(&Fields, 0x30, &Signed->Issuer) || !CiReadDer(&Fields, 2, &Signed->Serial) || Fields.Size)
        return 0;
    if (!CiReadAlgorithm(&Inner, &SignerDigest) || SignerDigest != Signed->DigestType || !CiReadDer(&Inner, 0xa0, &Signed->Attributes))
        return 0;
    if (!CiReadDer(&Inner, 0x30, &Value))
        return 0;
    Fields = CiContents(Value);
    if (!CiReadDer(&Fields, 6, &Oid))
        return 0;
    /* RSA PKCS#1 v1.5; reject an unimplemented algorithm instead of guessing. */
    if (!CI_OID_EQUAL(Oid, MBEDTLS_OID_PKCS1_RSA) && !CI_OID_EQUAL(Oid, MBEDTLS_OID_PKCS1_SHA256) && !CI_OID_EQUAL(Oid, MBEDTLS_OID_PKCS1_SHA384) && !CI_OID_EQUAL(Oid, MBEDTLS_OID_PKCS1_SHA512))
        return 0;
    if (Fields.Size && (!CiReadDer(&Fields, 5, &Value) || Value.Size || Fields.Size))
        return 0;
    if (!CiReadDer(&Inner, 4, &Signed->Signature))
        return 0;
    if (Inner.Size && !CiReadDer(&Inner, 0xa1, &Signed->ExtraAttributes))
        return 0;
    return !Inner.Size;
}

static int
CiFindAttribute(CI_DER Attributes, const char *Oid, size_t OidSize, CI_DER *Result)
{
    CI_CURSOR Cursor = CiContents(Attributes), Inner, Values;
    CI_DER Sequence, Type, Set;
    int Found = 0;

    while (Cursor.Size)
    {
        if (!CiReadDer(&Cursor, 0x30, &Sequence))
            return 0;
        Inner = CiContents(Sequence);
        if (!CiReadDer(&Inner, 6, &Type) || !CiReadDer(&Inner, 0x31, &Set) || Inner.Size)
            return 0;
        if (Type.Size != OidSize || memcmp(Type.Data, Oid, OidSize))
            continue;
        Values = CiContents(Set);
        if (Found || !Values.Size || !CiReadDer(&Values, Values.Data[0], Result) || Values.Size)
            return 0;
        Found = 1;
    }
    return Found;
}

static int
CiHash(mbedtls_md_type_t Type, const unsigned char *Data, size_t Size, unsigned char Digest[64])
{
    const mbedtls_md_info_t *Info = mbedtls_md_info_from_type(Type);
    return Info && !mbedtls_md(Info, Data, Size, Digest);
}

static int
CiCheckAttributes(CI_SIGNED_DATA *Signed, unsigned char Digest[64])
{
    CI_DER Type, Hash;
    mbedtls_md_context_t Context;
    const mbedtls_md_info_t *Info = mbedtls_md_info_from_type(Signed->DigestType);
    unsigned char SetTag = 0x31;
    int Result = 0;

    if (!Info || !CiFindAttribute(Signed->Attributes, CiContentTypeOid, sizeof(CiContentTypeOid) - 1, &Type) || !CiEqual(Type, Signed->ContentType))
        return 0;
    if (!CiFindAttribute(Signed->Attributes, CiMessageDigestOid, sizeof(CiMessageDigestOid) - 1, &Hash) || Hash.Tag != 4 || Hash.Size != mbedtls_md_get_size(Info))
        return 0;
    if (!CiHash(Signed->DigestType, Signed->Content.Data, Signed->Content.Size, Digest) || memcmp(Hash.Data, Digest, Hash.Size))
        return 0;
    mbedtls_md_init(&Context);
    if (!mbedtls_md_setup(&Context, Info, 0) && !mbedtls_md_starts(&Context) && !mbedtls_md_update(&Context, &SetTag, 1) && !mbedtls_md_update(&Context, Signed->Attributes.Start + 1, CiEncodedSize(Signed->Attributes) - 1) && !mbedtls_md_finish(&Context, Digest))
        Result = 1;
    mbedtls_md_free(&Context);
    return Result;
}

static uint64_t
CiTime(const mbedtls_x509_time *Time)
{
    static const unsigned short BeforeMonth[] = {0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    uint64_t Days;
    int Year = Time->year - 1;

    if (Time->year < 1970 || Time->mon < 1 || Time->mon > 12 || Time->day < 1 || Time->day > 31)
        return 0;
    Days = 365ULL * (Time->year - 1970) + Year / 4 - Year / 100 + Year / 400 - 477;
    Days += BeforeMonth[Time->mon] + Time->day - 1;
    if (Time->mon > 2 && !(Time->year % 4) && (Time->year % 100 || !(Time->year % 400)))
        ++Days;
    return ((Days * 24 + Time->hour) * 60 + Time->min) * 60 + Time->sec;
}

static int
CiCheckCertificateTime(void *Context, mbedtls_x509_crt *Certificate, int Depth, uint32_t *Flags)
{
    uint64_t Time = *(uint64_t *)Context;
    (void)Depth;
    if (Time < CiTime(&Certificate->valid_from))
        *Flags |= MBEDTLS_X509_BADCERT_FUTURE;
    if (Time > CiTime(&Certificate->valid_to))
        *Flags |= MBEDTLS_X509_BADCERT_EXPIRED;
    return 0;
}

static int
CiMicrosoftPublisher(const mbedtls_x509_crt *Certificate)
{
    const mbedtls_x509_name *Name;
    static const char Microsoft[] = "Microsoft Corporation";

    for (Name = &Certificate->subject; Name; Name = Name->next)
    {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORGANIZATION, &Name->oid) == 0 && Name->val.len == sizeof(Microsoft) - 1 && !memcmp(Name->val.p, Microsoft, sizeof(Microsoft) - 1))
            return 1;
    }
    return 0;
}

static int
CiVerifySigner(CI_SIGNED_DATA *Signed, uint64_t Time, int Timestamp)
{
    mbedtls_x509_crt Chain, Roots, Candidate;
    unsigned char Digest[64];
    unsigned int Index, Leaf = CI_MAX_CERTIFICATES;
    uint32_t Flags;
    int Result = 0;
    const char *Eku = Timestamp ? MBEDTLS_OID_TIME_STAMPING : MBEDTLS_OID_CODE_SIGNING;
    const mbedtls_x509_crt_profile Profile = {
        MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA256) | MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA384) | MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA512),
        MBEDTLS_X509_ID_FLAG(MBEDTLS_PK_RSA), 0, 2048};

    if (!CiCheckAttributes(Signed, Digest))
        return 0;
    mbedtls_x509_crt_init(&Chain);
    mbedtls_x509_crt_init(&Roots);
    for (Index = 0; Index < Signed->CertificateCount; ++Index)
    {
        mbedtls_x509_crt_init(&Candidate);
        if (!mbedtls_x509_crt_parse_der(&Candidate, Signed->Certificates[Index].Start, CiEncodedSize(Signed->Certificates[Index])) && Candidate.issuer_raw.len == CiEncodedSize(Signed->Issuer) && !memcmp(Candidate.issuer_raw.p, Signed->Issuer.Start, Candidate.issuer_raw.len) && Candidate.serial.len == Signed->Serial.Size && !memcmp(Candidate.serial.p, Signed->Serial.Data, Candidate.serial.len))
            Leaf = Index;
        mbedtls_x509_crt_free(&Candidate);
        if (Leaf != CI_MAX_CERTIFICATES)
            break;
    }
    if (Leaf == CI_MAX_CERTIFICATES || mbedtls_x509_crt_parse_der(&Chain, Signed->Certificates[Leaf].Start, CiEncodedSize(Signed->Certificates[Leaf])))
        goto Exit;
    for (Index = 0; Index < Signed->CertificateCount; ++Index)
    {
        if (Index != Leaf && mbedtls_x509_crt_parse_der(&Chain, Signed->Certificates[Index].Start, CiEncodedSize(Signed->Certificates[Index])))
            goto Exit;
    }
    if (mbedtls_x509_crt_parse_der(&Roots, CiMicrosoftRoot2010, sizeof(CiMicrosoftRoot2010)) || mbedtls_x509_crt_parse_der(&Roots, CiMicrosoftRoot2011, sizeof(CiMicrosoftRoot2011)))
        goto Exit;
    if (mbedtls_x509_crt_check_key_usage(&Chain, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) || !(Chain.ext_types & MBEDTLS_X509_EXT_EXTENDED_KEY_USAGE) || mbedtls_x509_crt_check_extended_key_usage(&Chain, Eku, strlen(Eku)))
        goto Exit;
    if (!Timestamp && !CiMicrosoftPublisher(&Chain))
        goto Exit;
    if (mbedtls_x509_crt_verify_with_profile(&Chain, &Roots, NULL, &Profile, NULL, &Flags, CiCheckCertificateTime, &Time))
        goto Exit;
    if (mbedtls_pk_verify(&Chain.pk, Signed->DigestType, Digest, 0, Signed->Signature.Data, Signed->Signature.Size))
        goto Exit;
    Result = 1;
Exit:
    mbedtls_x509_crt_free(&Chain);
    mbedtls_x509_crt_free(&Roots);
    return Result;
}

static int
CiVerifyTimestamp(CI_SIGNED_DATA *Signed, uint64_t Now, uint64_t *SigningTime)
{
    CI_SIGNED_DATA Timestamp;
    CI_DER Attribute, Sequence, Value, Hash;
    CI_CURSOR Cursor, Inner;
    mbedtls_md_type_t Type;
    mbedtls_x509_time Time;
    unsigned char Digest[64];
    unsigned int Index, Fields[6] = {0};
    static const unsigned int Digits[] = {4, 2, 2, 2, 2, 2};
    size_t Position = 0;

    if (!Signed->ExtraAttributes.Size)
        return 0;
    if (!CiFindAttribute(Signed->ExtraAttributes, CiMicrosoftTimestampOid, sizeof(CiMicrosoftTimestampOid) - 1, &Attribute) && !CiFindAttribute(Signed->ExtraAttributes, CiTimestampOid, sizeof(CiTimestampOid) - 1, &Attribute))
        return 0;
    if (Attribute.Tag != 0x30 || !CiParseSignedData(Attribute, 1, &Timestamp))
        return 0;
    Cursor = CiContents(Timestamp.Content);
    if (!CiReadDer(&Cursor, 0x30, &Sequence) || Cursor.Size)
        return 0;
    Cursor = CiContents(Sequence);
    if (!CiReadDer(&Cursor, 2, &Value) || Value.Size != 1 || Value.Data[0] != 1 || !CiReadDer(&Cursor, 6, &Value) || !CiReadDer(&Cursor, 0x30, &Sequence))
        return 0;
    Inner = CiContents(Sequence);
    if (!CiReadAlgorithm(&Inner, &Type) || !CiReadDer(&Inner, 4, &Hash) || Inner.Size || Hash.Size != mbedtls_md_get_size(mbedtls_md_info_from_type(Type)))
        return 0;
    if (!CiHash(Type, Signed->Signature.Data, Signed->Signature.Size, Digest) || memcmp(Hash.Data, Digest, Hash.Size))
        return 0;
    if (!CiReadDer(&Cursor, 2, &Value) || !CiReadDer(&Cursor, 0x18, &Value) || Value.Size < 15 || Value.Data[Value.Size - 1] != 'Z')
        return 0;
    for (Index = 0; Index < 6; ++Index)
    {
        unsigned int Digit;
        for (Digit = 0; Digit < Digits[Index]; ++Digit)
        {
            unsigned int Character = Value.Data[Position++];
            if (Character < '0' || Character > '9')
                return 0;
            Fields[Index] = Fields[Index] * 10 + Character - '0';
        }
    }
    if (Position != Value.Size - 1)
    {
        if (Value.Data[Position++] != '.' || Position == Value.Size - 1)
            return 0;
        while (Position < Value.Size - 1)
        {
            if (Value.Data[Position] < '0' || Value.Data[Position++] > '9')
                return 0;
        }
    }
    Time.year = Fields[0]; Time.mon = Fields[1]; Time.day = Fields[2];
    Time.hour = Fields[3]; Time.min = Fields[4]; Time.sec = Fields[5];
    *SigningTime = CiTime(&Time);
    if (!*SigningTime || *SigningTime > Now || Time.hour > 23 || Time.min > 59 || Time.sec > 59)
        return 0;
    return CiVerifySigner(&Timestamp, *SigningTime, 1);
}

static unsigned int
CiU16(const unsigned char *Bytes)
{
    return Bytes[0] | (unsigned int)Bytes[1] << 8;
}

static uint32_t
CiU32(const unsigned char *Bytes)
{
    return CiU16(Bytes) | (uint32_t)CiU16(Bytes + 2) << 16;
}

static int
CiHashRange(CI_READ_FILE Read, void *File, uint64_t Start, uint64_t End, mbedtls_md_context_t *Hash, unsigned char *Buffer)
{
    size_t Length;
    while (Start < End)
    {
        Length = End - Start > CI_HASH_BUFFER_SIZE ? CI_HASH_BUFFER_SIZE : (size_t)(End - Start);
        if (!Read(File, Start, Buffer, Length) || mbedtls_md_update(Hash, Buffer, Length))
            return 0;
        Start += Length;
    }
    return Start == End;
}

int
CiHashFile(CI_READ_FILE Read, void *Context, uint64_t Size, unsigned char Digest[32])
{
    mbedtls_md_context_t Hash;
    unsigned char *Buffer = CiAllocate(1, CI_HASH_BUFFER_SIZE);
    int Result = 0;

    if (!Buffer)
        return 0;
    mbedtls_md_init(&Hash);
    if (!mbedtls_md_setup(&Hash, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) && !mbedtls_md_starts(&Hash) && CiHashRange(Read, Context, 0, Size, &Hash, Buffer) && !mbedtls_md_finish(&Hash, Digest))
        Result = 1;
    mbedtls_md_free(&Hash);
    CiFree(Buffer);
    return Result;
}

int
CiVerifyMicrosoftImage(CI_READ_FILE Read, void *Context, uint64_t Size, uint64_t Now)
{
    unsigned char Header[264], Section[40], Digest[64];
    uint32_t NtOffset, OptionalSize, DirectoryOffset, HeaderSize, CertificateOffset, CertificateSize, Count, Index, Other;
    struct {uint32_t Offset, Size;} Sections[96], Swap;
    uint64_t SectionTable, Sum, PreviousEnd;
    unsigned char *Certificates = NULL, *Buffer = NULL;
    mbedtls_md_context_t Hash;
    CI_SIGNED_DATA Signed;
    CI_DER ContentInfo, Value, Sequence, ImageHash;
    CI_CURSOR Cursor, Inner;
    mbedtls_md_type_t ImageDigestType;
    uint64_t SigningTime = Now;
    int Result = 0;

    mbedtls_md_init(&Hash);
    if (Size < sizeof(Header) || !Read(Context, 0, Header, 64) || CiU16(Header) != 0x5a4d)
        goto Exit;
    NtOffset = CiU32(Header + 60);
    if (NtOffset > Size - sizeof(Header) || !Read(Context, NtOffset, Header, sizeof(Header)) || CiU32(Header) != 0x4550)
        goto Exit;
    Count = CiU16(Header + 6);
    OptionalSize = CiU16(Header + 20);
    if (!Count || Count > 96)
        goto Exit;
    if (CiU16(Header + 24) == 0x20b)
        DirectoryOffset = 24 + 112;
    else if (CiU16(Header + 24) == 0x10b)
        DirectoryOffset = 24 + 96;
    else
        goto Exit;
    if (OptionalSize < DirectoryOffset - 24 + 40 || CiU32(Header + DirectoryOffset - 4) < 5)
        goto Exit;
    HeaderSize = CiU32(Header + 24 + 60);
    CertificateOffset = CiU32(Header + DirectoryOffset + 32);
    CertificateSize = CiU32(Header + DirectoryOffset + 36);
    SectionTable = (uint64_t)NtOffset + 24 + OptionalSize;
    if (CertificateSize < 8 || CertificateSize > CI_MAX_CERTIFICATE_BYTES || CertificateOffset > Size || CertificateSize != Size - CertificateOffset || (CertificateOffset & 7) || HeaderSize > CertificateOffset || SectionTable + Count * 40 > HeaderSize)
        goto Exit;
    PreviousEnd = HeaderSize;
    Sum = HeaderSize;
    for (Index = 0; Index < Count; ++Index)
    {
        if (!Read(Context, SectionTable + Index * 40, Section, sizeof(Section)))
            goto Exit;
        Sections[Index].Size = CiU32(Section + 16);
        Sections[Index].Offset = CiU32(Section + 20);
        Sum += Sections[Index].Size;
    }
    for (Index = 1; Index < Count; ++Index)
    {
        Swap = Sections[Index];
        Other = Index;
        while (Other && Sections[Other - 1].Offset > Swap.Offset)
        {
            Sections[Other] = Sections[Other - 1];
            --Other;
        }
        Sections[Other] = Swap;
    }
    for (Index = 0; Index < Count; ++Index)
    {
        if (!Sections[Index].Size)
            continue;
        if (Sections[Index].Offset < PreviousEnd || Sections[Index].Offset > CertificateOffset || Sections[Index].Size > CertificateOffset - Sections[Index].Offset)
            goto Exit;
        PreviousEnd = (uint64_t)Sections[Index].Offset + Sections[Index].Size;
    }
    if (Sum > CertificateOffset)
        goto Exit;
    Certificates = CiAllocate(1, CertificateSize);
    Buffer = CiAllocate(1, CI_HASH_BUFFER_SIZE);
    if (!Certificates || !Buffer || !Read(Context, CertificateOffset, Certificates, CertificateSize))
        goto Exit;
    /* One PKCS#7 WIN_CERTIFICATE. Additional signature formats fail closed. */
    if (CiU32(Certificates) != CertificateSize || CiU16(Certificates + 4) != 0x200 || CiU16(Certificates + 6) != 2)
        goto Exit;
    Cursor.Data = Certificates + 8;
    Cursor.Size = CertificateSize - 8;
    if (!CiReadDer(&Cursor, 0x30, &ContentInfo))
        goto Exit;
    while (Cursor.Size)
    {
        if (*Cursor.Data++)
            goto Exit;
        --Cursor.Size;
    }
    if (!CiParseSignedData(ContentInfo, 0, &Signed))
        goto Exit;
    Cursor = CiContents(Signed.Content);
    if (!CiReadDer(&Cursor, 0x30, &Sequence))
        goto Exit;
    Inner = CiContents(Sequence);
    if (!CiReadDer(&Inner, 6, &Value) || !CI_OID_EQUAL(Value, CiSpcPeImageOid))
        goto Exit;
    if (!CiReadDer(&Cursor, 0x30, &Sequence) || Cursor.Size)
        goto Exit;
    Inner = CiContents(Sequence);
    if (!CiReadAlgorithm(&Inner, &ImageDigestType) || !CiReadDer(&Inner, 4, &ImageHash) || Inner.Size || ImageHash.Size != mbedtls_md_get_size(mbedtls_md_info_from_type(ImageDigestType)))
        goto Exit;
    if (mbedtls_md_setup(&Hash, mbedtls_md_info_from_type(ImageDigestType), 0) || mbedtls_md_starts(&Hash))
        goto Exit;
    if (!CiHashRange(Read, Context, 0, (uint64_t)NtOffset + 24 + 64, &Hash, Buffer) || !CiHashRange(Read, Context, (uint64_t)NtOffset + 24 + 68, (uint64_t)NtOffset + DirectoryOffset + 32, &Hash, Buffer) || !CiHashRange(Read, Context, (uint64_t)NtOffset + DirectoryOffset + 40, HeaderSize, &Hash, Buffer))
        goto Exit;
    for (Index = 0; Index < Count; ++Index)
    {
        if (!CiHashRange(Read, Context, Sections[Index].Offset, (uint64_t)Sections[Index].Offset + Sections[Index].Size, &Hash, Buffer))
            goto Exit;
    }
    if (!CiHashRange(Read, Context, Sum, CertificateOffset, &Hash, Buffer) || mbedtls_md_finish(&Hash, Digest) || memcmp(Digest, ImageHash.Data, ImageHash.Size))
        goto Exit;
    if (!CiVerifyTimestamp(&Signed, Now, &SigningTime))
        SigningTime = Now;
    Result = CiVerifySigner(&Signed, SigningTime, 0);
Exit:
    mbedtls_md_free(&Hash);
    CiFree(Certificates);
    CiFree(Buffer);
    return Result;
}
