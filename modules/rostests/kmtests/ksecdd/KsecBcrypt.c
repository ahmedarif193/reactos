/*
 * PROJECT:     ReactOS Kernel-Mode Test Suite
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Validate the kernel BCrypt primitive surface with known answers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include <kmt_test.h>
#include <bcrypt.h>

#define TEST_TAG 'tCbK'

static const WCHAR AesCmacAlgorithm[] = L"AES-CMAC";
static const WCHAR Sp800108Algorithm[] = L"SP800_108_CTR_HMAC";

static const UCHAR Sha256Abc[] =
{
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
};

static const UCHAR HmacSha256QuickFox[] =
{
    0xf7, 0xbc, 0x83, 0xf4, 0x30, 0x53, 0x84, 0x24,
    0xb1, 0x32, 0x98, 0xe6, 0xaa, 0x6f, 0xb1, 0x43,
    0xef, 0x4d, 0x59, 0xa1, 0x49, 0x46, 0x17, 0x59,
    0x97, 0x47, 0x9d, 0xbc, 0x2d, 0x1a, 0x3c, 0xd8
};

static const UCHAR AesCmacEmpty[] =
{
    0xbb, 0x1d, 0x69, 0x29, 0xe9, 0x59, 0x37, 0x28,
    0x7f, 0xa3, 0x7d, 0x12, 0x9b, 0x75, 0x67, 0x46
};

static const UCHAR Pbkdf2Sha256OneIteration[] =
{
    0x12, 0x0f, 0xb6, 0xcf, 0xfc, 0xf8, 0xb3, 0x2c,
    0x43, 0xe7, 0x22, 0x52, 0x56, 0xc4, 0xf8, 0x37,
    0xa8, 0x65, 0x48, 0xc9, 0x2c, 0xcc, 0x35, 0x48,
    0x08, 0x05, 0x98, 0x7c, 0xb7, 0x0b, 0xe1, 0x7b
};

static const UCHAR Sp800108HmacSha256[] =
{
    0x3d, 0xb7, 0x7b, 0x1c, 0x21, 0x6e, 0x64, 0x41,
    0xc3, 0x01, 0xee, 0xf7, 0x0b, 0x5e, 0x39, 0x47,
    0xb8, 0x31, 0x83, 0x16, 0xd4, 0xa8, 0x2a, 0x7e,
    0x78, 0xbb, 0x56, 0xb8, 0xd2, 0xa3, 0x41, 0x8b
};

static VOID
CheckBytes(
    _In_ PCSTR Name,
    _In_reads_bytes_(Size) const UCHAR *Actual,
    _In_reads_bytes_(Size) const UCHAR *Expected,
    _In_ SIZE_T Size)
{
    ok(RtlCompareMemory(Actual, Expected, Size) == Size,
       "%s known-answer mismatch\n", Name);
}

static VOID
TestSha256(VOID)
{
    static const UCHAR Input[] = {'a', 'b', 'c'};
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_HASH_HANDLE Hash = NULL;
    PUCHAR Object = NULL;
    UCHAR Output[sizeof(Sha256Abc)];
    ULONG ObjectSize = 0, ResultSize = 0;
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         BCRYPT_SHA256_ALGORITHM,
                                         NULL,
                                         BCRYPT_HASH_REUSABLE_FLAG);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = BCryptGetProperty(Algorithm, BCRYPT_OBJECT_LENGTH,
                               (PUCHAR)&ObjectSize, sizeof(ObjectSize),
                               &ResultSize, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ResultSize, sizeof(ObjectSize));
    ok(ObjectSize != 0, "zero hash object size\n");
    Object = ExAllocatePoolWithTag(NonPagedPoolNx, ObjectSize, TEST_TAG);
    ok(Object != NULL, "hash object allocation failed\n");
    if (Object == NULL)
        goto Cleanup;

    Status = BCryptCreateHash(Algorithm, &Hash, Object, ObjectSize,
                              NULL, 0, BCRYPT_HASH_REUSABLE_FLAG);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = BCryptHashData(Hash, (PUCHAR)Input, sizeof(Input), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = BCryptFinishHash(Hash, Output, sizeof(Output), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckBytes("SHA-256", Output, Sha256Abc, sizeof(Output));

    Status = BCryptHashData(Hash, (PUCHAR)Input, sizeof(Input), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = BCryptFinishHash(Hash, Output, sizeof(Output), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckBytes("reusable SHA-256", Output, Sha256Abc, sizeof(Output));

Cleanup:
    if (Hash != NULL)
        ok_eq_hex(BCryptDestroyHash(Hash), STATUS_SUCCESS);
    if (Object != NULL)
        ExFreePoolWithTag(Object, TEST_TAG);
    ok_eq_hex(BCryptCloseAlgorithmProvider(Algorithm, 0), STATUS_SUCCESS);
}

static VOID
TestHmacSha256(VOID)
{
    static const UCHAR Key[] = {'k', 'e', 'y'};
    static const UCHAR Input[] = "The quick brown fox jumps over the lazy dog";
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_HASH_HANDLE Hash = NULL;
    UCHAR Output[sizeof(HmacSha256QuickFox)];
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM,
                                         NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = BCryptCreateHash(Algorithm, &Hash, NULL, 0,
                              (PUCHAR)Key, sizeof(Key), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(BCryptHashData(Hash, (PUCHAR)Input, sizeof(Input) - 1, 0),
                  STATUS_SUCCESS);
        ok_eq_hex(BCryptFinishHash(Hash, Output, sizeof(Output), 0),
                  STATUS_SUCCESS);
        CheckBytes("HMAC-SHA-256", Output, HmacSha256QuickFox, sizeof(Output));
        ok_eq_hex(BCryptDestroyHash(Hash), STATUS_SUCCESS);
    }
    ok_eq_hex(BCryptCloseAlgorithmProvider(Algorithm, 0), STATUS_SUCCESS);
}

static VOID
TestAesCmac(VOID)
{
    static const UCHAR Key[] =
    {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_HASH_HANDLE Hash = NULL;
    UCHAR Output[sizeof(AesCmacEmpty)];
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Algorithm, AesCmacAlgorithm, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = BCryptCreateHash(Algorithm, &Hash, NULL, 0,
                              (PUCHAR)Key, sizeof(Key), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(BCryptHashData(Hash, NULL, 0, 0), STATUS_SUCCESS);
        ok_eq_hex(BCryptFinishHash(Hash, Output, sizeof(Output), 0),
                  STATUS_SUCCESS);
        CheckBytes("AES-CMAC", Output, AesCmacEmpty, sizeof(Output));
        ok_eq_hex(BCryptDestroyHash(Hash), STATUS_SUCCESS);
    }
    ok_eq_hex(BCryptCloseAlgorithmProvider(Algorithm, 0), STATUS_SUCCESS);
}

static VOID
TestAesCbcDecrypt(VOID)
{
    static const UCHAR KeyBytes[] =
    {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const UCHAR InitialVector[] =
    {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const UCHAR CipherText[] =
    {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d
    };
    static const UCHAR PlainText[] =
    {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_KEY_HANDLE Key = NULL;
    UCHAR Iv[sizeof(InitialVector)], Output[sizeof(PlainText)];
    ULONG ResultSize = 0;
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_AES_ALGORITHM, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = BCryptSetProperty(Algorithm, BCRYPT_CHAINING_MODE,
                               (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                               sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = BCryptGenerateSymmetricKey(Algorithm, &Key, NULL, 0,
                                        (PUCHAR)KeyBytes, sizeof(KeyBytes), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(Iv, InitialVector, sizeof(Iv));
        Status = BCryptDecrypt(Key, (PUCHAR)CipherText, sizeof(CipherText),
                               NULL, Iv, sizeof(Iv), Output, sizeof(Output),
                               &ResultSize, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(ResultSize, sizeof(Output));
        CheckBytes("AES-CBC decrypt", Output, PlainText, sizeof(Output));
        ok_eq_hex(BCryptDestroyKey(Key), STATUS_SUCCESS);
    }
    ok_eq_hex(BCryptCloseAlgorithmProvider(Algorithm, 0), STATUS_SUCCESS);
}

static VOID
TestKdf(
    _In_ PCWSTR AlgorithmName,
    _In_ BOOLEAN Sp800)
{
    static const UCHAR Password[] = "password";
    static const UCHAR Salt[] = "salt";
    static const UCHAR Sp800Key[20] =
    {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b
    };
    static const UCHAR Label[] = "label";
    static const UCHAR Context[] = "context";
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_KEY_HANDLE Key = NULL;
    BCryptBuffer Buffers[3];
    BCryptBufferDesc Description;
    UCHAR Output[32];
    UINT64 Iterations = 1;
    ULONG ResultSize = 0;
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Algorithm, AlgorithmName, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = BCryptGenerateSymmetricKey(Algorithm, &Key, NULL, 0,
                                        (PUCHAR)(Sp800 ? Sp800Key : Password),
                                        Sp800 ? sizeof(Sp800Key) : sizeof(Password) - 1,
                                        0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Buffers[0].BufferType = KDF_HASH_ALGORITHM;
    Buffers[0].pvBuffer = (PVOID)BCRYPT_SHA256_ALGORITHM;
    Buffers[0].cbBuffer = sizeof(BCRYPT_SHA256_ALGORITHM);
    if (Sp800)
    {
        Buffers[1].BufferType = KDF_LABEL;
        Buffers[1].pvBuffer = (PVOID)Label;
        Buffers[1].cbBuffer = sizeof(Label) - 1;
        Buffers[2].BufferType = KDF_CONTEXT;
        Buffers[2].pvBuffer = (PVOID)Context;
        Buffers[2].cbBuffer = sizeof(Context) - 1;
    }
    else
    {
        Buffers[1].BufferType = KDF_SALT;
        Buffers[1].pvBuffer = (PVOID)Salt;
        Buffers[1].cbBuffer = sizeof(Salt) - 1;
        Buffers[2].BufferType = KDF_ITERATION_COUNT;
        Buffers[2].pvBuffer = &Iterations;
        Buffers[2].cbBuffer = sizeof(Iterations);
    }
    Description.ulVersion = BCRYPTBUFFER_VERSION;
    Description.cBuffers = RTL_NUMBER_OF(Buffers);
    Description.pBuffers = Buffers;

    Status = BCryptKeyDerivation(Key, &Description, Output, sizeof(Output),
                                 &ResultSize, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ResultSize, sizeof(Output));
    if (NT_SUCCESS(Status))
        CheckBytes(Sp800 ? "SP800-108-HMAC-SHA-256" : "PBKDF2-HMAC-SHA-256",
                   Output,
                   Sp800 ? Sp800108HmacSha256 : Pbkdf2Sha256OneIteration,
                   sizeof(Output));
    ok_eq_hex(BCryptDestroyKey(Key), STATUS_SUCCESS);

Cleanup:
    ok_eq_hex(BCryptCloseAlgorithmProvider(Algorithm, 0), STATUS_SUCCESS);
}

static VOID
TestRandom(VOID)
{
    UCHAR First[32] = {0}, Second[32] = {0};
    UCHAR Zero[32] = {0};

    ok_eq_hex(BCryptGenRandom(NULL, First, sizeof(First),
                              BCRYPT_USE_SYSTEM_PREFERRED_RNG), STATUS_SUCCESS);
    ok_eq_hex(BCryptGenRandom(NULL, Second, sizeof(Second),
                              BCRYPT_USE_SYSTEM_PREFERRED_RNG), STATUS_SUCCESS);
    ok(RtlCompareMemory(First, Zero, sizeof(First)) != sizeof(First),
       "RNG returned all zeroes\n");
    ok(RtlCompareMemory(First, Second, sizeof(First)) != sizeof(First),
       "two RNG calls returned identical output\n");
}

START_TEST(KsecBcrypt)
{
    LONG SuccessesBefore = ResultBuffer->Successes;
    LONG FailuresBefore = ResultBuffer->Failures;

    TestSha256();
    TestHmacSha256();
    TestAesCmac();
    TestAesCbcDecrypt();
    TestKdf(BCRYPT_PBKDF2_ALGORITHM, FALSE);
    TestKdf(Sp800108Algorithm, TRUE);
    TestRandom();

    if (ResultBuffer->Failures == FailuresBefore)
    {
        DbgPrint("KSEC_BCRYPT_KMTEST_PASS checks=%ld\n",
                 ResultBuffer->Successes - SuccessesBefore);
    }
}
