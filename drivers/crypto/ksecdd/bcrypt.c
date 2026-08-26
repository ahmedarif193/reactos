/*
 * PROJECT:     ReactOS Kernel Security Support Provider Interface Driver
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-mode CNG primitive provider
 * COPYRIGHT:   Copyright 2009 Henri Verbeet for CodeWeavers
 *              Copyright 2026 Ahmed Arif
 *
 * This is a kernel-mode adaptation of the handle and SymCrypt conventions used
 * by Wine's bcrypt implementation.  It deliberately exposes only primitives
 * implemented below; unknown algorithms and properties fail closed.
 */

#include "ksecdd.h"

#include <bcrypt.h>
#include "symcrypt.h"
#include "sc_lib.h"

#define NDEBUG
#include <debug.h>

#define KSEC_BCRYPT_TAG          'gCbK'
#define KSEC_BCRYPT_ALLOC_MAGIC  0x4b434241
#define KSEC_BCRYPT_ALIGNMENT    32

#define KSEC_MAGIC_ALGORITHM     0x30474c41
#define KSEC_MAGIC_HASH          0x48534148
#define KSEC_MAGIC_KEY           0x3059454b

#define KSEC_OBJECT_EXTERNAL     0x00000001
#define KSEC_HASH_HMAC           0x00000002
#define KSEC_HASH_REUSABLE       0x00000004
#define KSEC_KEY_PRIVATE         0x00000008

#define KSEC_AES_BLOCK_SIZE      16

static const WCHAR KsecAesCmacAlgorithm[] = L"AES-CMAC";
static const WCHAR KsecSp800108Algorithm[] = L"SP800_108_CTR_HMAC";

typedef enum _KSEC_BCRYPT_ALGORITHM_ID
{
    KsecAlgInvalid,
    KsecAlgSha256,
    KsecAlgSha384,
    KsecAlgSha512,
    KsecAlgAes,
    KsecAlgAesCmac,
    KsecAlgRng,
    KsecAlgRsa,
    KsecAlgRsaSign,
    KsecAlgEcdsaP256,
    KsecAlgPbkdf2,
    KsecAlgSp800108
} KSEC_BCRYPT_ALGORITHM_ID;

typedef enum _KSEC_BCRYPT_CHAIN_MODE
{
    KsecChainCbc,
    KsecChainEcb
} KSEC_BCRYPT_CHAIN_MODE;

typedef struct _KSEC_BCRYPT_OBJECT
{
    ULONG Magic;
    ULONG Flags;
} KSEC_BCRYPT_OBJECT;

typedef struct _KSEC_BCRYPT_ALGORITHM
{
    KSEC_BCRYPT_OBJECT Header;
    KSEC_BCRYPT_ALGORITHM_ID Id;
    KSEC_BCRYPT_CHAIN_MODE ChainMode;
} KSEC_BCRYPT_ALGORITHM;

typedef struct _KSEC_BCRYPT_HASH
{
    KSEC_BCRYPT_OBJECT Header;
    KSEC_BCRYPT_ALGORITHM_ID Id;
    ULONG ResultLength;
    PCSYMCRYPT_HASH HashAlgorithm;
    union
    {
        SYMCRYPT_HASH_STATE Hash;
        struct
        {
            SYMCRYPT_HMAC_EXPANDED_KEY Key;
            SYMCRYPT_HMAC_STATE State;
        } Hmac;
        struct
        {
            SYMCRYPT_AES_CMAC_EXPANDED_KEY Key;
            SYMCRYPT_AES_CMAC_STATE State;
        } Cmac;
    } State;
} KSEC_BCRYPT_HASH;

typedef struct _KSEC_BCRYPT_KEY
{
    KSEC_BCRYPT_OBJECT Header;
    KSEC_BCRYPT_ALGORITHM_ID Id;
    KSEC_BCRYPT_CHAIN_MODE ChainMode;
    ULONG BitLength;
    FAST_MUTEX Lock;
    union
    {
        struct
        {
            SYMCRYPT_AES_EXPANDED_KEY ExpandedKey;
            UCHAR Vector[KSEC_AES_BLOCK_SIZE];
        } Aes;
        struct
        {
            PUCHAR Secret;
            ULONG SecretLength;
        } Kdf;
        struct
        {
            PSYMCRYPT_RSAKEY Key;
        } Rsa;
        struct
        {
            PSYMCRYPT_ECURVE Curve;
            PSYMCRYPT_ECKEY Key;
        } Ecc;
    } Data;
} KSEC_BCRYPT_KEY;

typedef struct _KSEC_BCRYPT_ALLOCATION
{
    PVOID RawPointer;
    SIZE_T Size;
    ULONG Magic;
} KSEC_BCRYPT_ALLOCATION;

extern const SYMCRYPT_OID SymCryptSha256OidList[2];
extern const SYMCRYPT_OID SymCryptSha384OidList[2];
extern const SYMCRYPT_OID SymCryptSha512OidList[2];

UINT32 g_SymCryptFipsSelftestsPerformed;

SYMCRYPT_ENVIRONMENT_DEFS(Generic);

static PVOID
KsecBcryptAllocate(
    _In_ SIZE_T Size)
{
    KSEC_BCRYPT_ALLOCATION *Allocation;
    ULONG_PTR Address;
    PVOID RawPointer;

    if (Size > MAXULONG_PTR - KSEC_BCRYPT_ALIGNMENT - sizeof(*Allocation))
        return NULL;

    RawPointer = ExAllocatePoolWithTag(NonPagedPoolNx,
                                       Size + KSEC_BCRYPT_ALIGNMENT + sizeof(*Allocation),
                                       KSEC_BCRYPT_TAG);
    if (RawPointer == NULL)
        return NULL;

    Address = ((ULONG_PTR)RawPointer + sizeof(*Allocation) +
               KSEC_BCRYPT_ALIGNMENT - 1) & ~(KSEC_BCRYPT_ALIGNMENT - 1);
    Allocation = (KSEC_BCRYPT_ALLOCATION *)(Address - sizeof(*Allocation));
    Allocation->RawPointer = RawPointer;
    Allocation->Size = Size;
    Allocation->Magic = KSEC_BCRYPT_ALLOC_MAGIC;
    RtlZeroMemory((PVOID)Address, Size);
    return (PVOID)Address;
}

static VOID
KsecBcryptFree(
    _In_opt_ PVOID Pointer)
{
    KSEC_BCRYPT_ALLOCATION *Allocation;
    PVOID RawPointer;

    if (Pointer == NULL)
        return;

    Allocation = (KSEC_BCRYPT_ALLOCATION *)((PUCHAR)Pointer - sizeof(*Allocation));
    if (Allocation->Magic != KSEC_BCRYPT_ALLOC_MAGIC)
        return;

    RawPointer = Allocation->RawPointer;
    RtlSecureZeroMemory(Pointer, Allocation->Size);
    RtlSecureZeroMemory(Allocation, sizeof(*Allocation));
    ExFreePoolWithTag(RawPointer, KSEC_BCRYPT_TAG);
}

PVOID
SYMCRYPT_CALL
SymCryptCallbackAlloc(
    SIZE_T Size)
{
    return KsecBcryptAllocate(Size);
}

VOID
SYMCRYPT_CALL
SymCryptCallbackFree(
    PVOID Pointer)
{
    KsecBcryptFree(Pointer);
}

SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptCallbackRandom(
    PBYTE Buffer,
    SIZE_T Size)
{
    return NT_SUCCESS(KsecGenRandom(Buffer, Size)) ?
           SYMCRYPT_NO_ERROR : SYMCRYPT_EXTERNAL_FAILURE;
}

PVOID
SYMCRYPT_CALL
SymCryptCallbackAllocateMutexFastInproc(VOID)
{
    PFAST_MUTEX Mutex = KsecBcryptAllocate(sizeof(*Mutex));

    if (Mutex != NULL)
        ExInitializeFastMutex(Mutex);
    return Mutex;
}

VOID
SYMCRYPT_CALL
SymCryptCallbackFreeMutexFastInproc(
    PVOID Mutex)
{
    KsecBcryptFree(Mutex);
}

VOID
SYMCRYPT_CALL
SymCryptCallbackAcquireMutexFastInproc(
    PVOID Mutex)
{
    ExAcquireFastMutex((PFAST_MUTEX)Mutex);
}

VOID
SYMCRYPT_CALL
SymCryptCallbackReleaseMutexFastInproc(
    PVOID Mutex)
{
    ExReleaseFastMutex((PFAST_MUTEX)Mutex);
}

SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptRsaSignVerifyPct(
    PCSYMCRYPT_RSAKEY Key)
{
    UNREFERENCED_PARAMETER(Key);
    return SYMCRYPT_NOT_IMPLEMENTED;
}

SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptDsaPct(
    PCSYMCRYPT_DLKEY Key)
{
    UNREFERENCED_PARAMETER(Key);
    return SYMCRYPT_NOT_IMPLEMENTED;
}

SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptEcDsaPct(
    PCSYMCRYPT_ECKEY Key)
{
    UNREFERENCED_PARAMETER(Key);
    return SYMCRYPT_NOT_IMPLEMENTED;
}

VOID SYMCRYPT_CALL SymCryptRsaSelftest(VOID) { SymCryptFatal('RsaT'); }
VOID SYMCRYPT_CALL SymCryptDsaSelftest(VOID) { SymCryptFatal('DsaT'); }
VOID SYMCRYPT_CALL SymCryptEcDsaSelftest(VOID) { SymCryptFatal('EcdT'); }
VOID SYMCRYPT_CALL SymCryptDhSecretAgreementSelftest(VOID) { SymCryptFatal('Dh T'); }
VOID SYMCRYPT_CALL SymCryptEcDhSecretAgreementSelftest(VOID) { SymCryptFatal('EDhT'); }

SYMCRYPT_CPU_FEATURES
SYMCRYPT_CALL
SymCryptCpuFeaturesNeverPresentEnvGeneric(VOID)
{
    return (SYMCRYPT_CPU_FEATURES)~0;
}

VOID
SYMCRYPT_CALL
SymCryptInitEnvGeneric(
    UINT32 Version)
{
    if (g_SymCryptFlags & SYMCRYPT_FLAG_LIB_INITIALIZED)
        return;

    g_SymCryptCpuFeaturesNotPresent = (SYMCRYPT_CPU_FEATURES)~0;
    SymCryptInitEnvCommon(Version);
}

DECLSPEC_NORETURN
VOID
SYMCRYPT_CALL
SymCryptFatalEnvGeneric(
    UINT32 FatalCode)
{
    KeBugCheckEx(0x139, FatalCode, 0, 0, 0);
    for (;;)
        NOTHING;
}

#if SYMCRYPT_CPU_AMD64 | SYMCRYPT_CPU_X86
SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptSaveXmmEnvGeneric(
    PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
    return SYMCRYPT_NOT_IMPLEMENTED;
}

VOID
SYMCRYPT_CALL
SymCryptRestoreXmmEnvGeneric(
    PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
}

SYMCRYPT_ERROR
SYMCRYPT_CALL
SymCryptSaveYmmEnvGeneric(
    PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
    return SYMCRYPT_NOT_IMPLEMENTED;
}

VOID
SYMCRYPT_CALL
SymCryptRestoreYmmEnvGeneric(
    PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
}

VOID
SYMCRYPT_CALL
SymCryptCpuidExFuncEnvGeneric(
    int CpuInfo[4],
    int Function,
    int SubFunction)
{
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(SubFunction);
    CpuInfo[0] = CpuInfo[1] = CpuInfo[2] = CpuInfo[3] = 0;
}
#endif

VOID
SYMCRYPT_CALL
SymCryptTestInjectErrorEnvGeneric(
    PBYTE Buffer,
    SIZE_T Size)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Size);
}

static BOOLEAN
KsecEqualWideString(
    _In_opt_ PCWSTR Left,
    _In_opt_ PCWSTR Right)
{
    if (Left == NULL || Right == NULL)
        return FALSE;

    while (*Left != UNICODE_NULL && *Right != UNICODE_NULL)
    {
        if (*Left++ != *Right++)
            return FALSE;
    }
    return *Left == *Right;
}

static SIZE_T
KsecWideStringBytes(
    _In_ PCWSTR String)
{
    PCWSTR Cursor = String;

    while (*Cursor != UNICODE_NULL)
        ++Cursor;
    return (Cursor - String + 1) * sizeof(WCHAR);
}

static BOOLEAN
KsecEqualWideBuffer(
    _In_reads_bytes_(Size) PCWSTR Buffer,
    _In_ ULONG Size,
    _In_ PCWSTR Expected)
{
    SIZE_T ExpectedSize = KsecWideStringBytes(Expected);

    return Buffer != NULL && Size >= ExpectedSize &&
           RtlCompareMemory(Buffer, Expected, ExpectedSize) == ExpectedSize;
}

static PCWSTR
KsecAlgorithmName(
    _In_ KSEC_BCRYPT_ALGORITHM_ID Id)
{
    switch (Id)
    {
        case KsecAlgSha256: return BCRYPT_SHA256_ALGORITHM;
        case KsecAlgSha384: return BCRYPT_SHA384_ALGORITHM;
        case KsecAlgSha512: return BCRYPT_SHA512_ALGORITHM;
        case KsecAlgAes: return BCRYPT_AES_ALGORITHM;
        case KsecAlgAesCmac: return KsecAesCmacAlgorithm;
        case KsecAlgRng: return BCRYPT_RNG_ALGORITHM;
        case KsecAlgRsa: return BCRYPT_RSA_ALGORITHM;
        case KsecAlgRsaSign: return BCRYPT_RSA_SIGN_ALGORITHM;
        case KsecAlgEcdsaP256: return BCRYPT_ECDSA_P256_ALGORITHM;
        case KsecAlgPbkdf2: return BCRYPT_PBKDF2_ALGORITHM;
        case KsecAlgSp800108: return KsecSp800108Algorithm;
        default: return NULL;
    }
}

static KSEC_BCRYPT_ALGORITHM_ID
KsecAlgorithmIdFromName(
    _In_ PCWSTR Name)
{
    KSEC_BCRYPT_ALGORITHM_ID Id;

    for (Id = KsecAlgSha256; Id <= KsecAlgSp800108; ++Id)
    {
        if (KsecEqualWideString(Name, KsecAlgorithmName(Id)))
            return Id;
    }
    return KsecAlgInvalid;
}

static PCSYMCRYPT_HASH
KsecSymCryptHash(
    _In_ KSEC_BCRYPT_ALGORITHM_ID Id)
{
    switch (Id)
    {
        case KsecAlgSha256: return SymCryptSha256Algorithm;
        case KsecAlgSha384: return SymCryptSha384Algorithm;
        case KsecAlgSha512: return SymCryptSha512Algorithm;
        default: return NULL;
    }
}

static PCSYMCRYPT_MAC
KsecSymCryptMacFromName(
    _In_reads_bytes_(Size) PCWSTR Name,
    _In_ ULONG Size)
{
    if (KsecEqualWideBuffer(Name, Size, BCRYPT_SHA256_ALGORITHM))
        return SymCryptHmacSha256Algorithm;
    if (KsecEqualWideBuffer(Name, Size, BCRYPT_SHA384_ALGORITHM))
        return SymCryptHmacSha384Algorithm;
    if (KsecEqualWideBuffer(Name, Size, BCRYPT_SHA512_ALGORITHM))
        return SymCryptHmacSha512Algorithm;
    return NULL;
}

static ULONG
KsecHashLength(
    _In_ KSEC_BCRYPT_ALGORITHM_ID Id)
{
    switch (Id)
    {
        case KsecAlgSha256: return SYMCRYPT_SHA256_RESULT_SIZE;
        case KsecAlgSha384: return SYMCRYPT_SHA384_RESULT_SIZE;
        case KsecAlgSha512: return SYMCRYPT_SHA512_RESULT_SIZE;
        case KsecAlgAesCmac: return SYMCRYPT_AES_CMAC_RESULT_SIZE;
        default: return 0;
    }
}

static ULONG
KsecHashBlockLength(
    _In_ KSEC_BCRYPT_ALGORITHM_ID Id)
{
    switch (Id)
    {
        case KsecAlgSha256: return SYMCRYPT_SHA256_INPUT_BLOCK_SIZE;
        case KsecAlgSha384: return SYMCRYPT_SHA384_INPUT_BLOCK_SIZE;
        case KsecAlgSha512: return SYMCRYPT_SHA512_INPUT_BLOCK_SIZE;
        case KsecAlgAesCmac: return SYMCRYPT_AES_CMAC_INPUT_BLOCK_SIZE;
        default: return 0;
    }
}

static const KSEC_BCRYPT_ALGORITHM KsecPseudoSha256 =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgSha256, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoSha384 =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgSha384, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoSha512 =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgSha512, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoHmacSha256 =
    {{KSEC_MAGIC_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG}, KsecAlgSha256, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoHmacSha384 =
    {{KSEC_MAGIC_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG}, KsecAlgSha384, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoHmacSha512 =
    {{KSEC_MAGIC_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG}, KsecAlgSha512, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoAesCmac =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgAesCmac, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoAesCbc =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgAes, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoAesEcb =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgAes, KsecChainEcb};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoRng =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgRng, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoRsa =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgRsa, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoRsaSign =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgRsaSign, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoEcdsaP256 =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgEcdsaP256, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoPbkdf2 =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgPbkdf2, KsecChainCbc};
static const KSEC_BCRYPT_ALGORITHM KsecPseudoSp800108 =
    {{KSEC_MAGIC_ALGORITHM, 0}, KsecAlgSp800108, KsecChainCbc};

static const KSEC_BCRYPT_ALGORITHM *
KsecGetPseudoAlgorithm(
    _In_ BCRYPT_ALG_HANDLE Handle)
{
    switch ((ULONG_PTR)Handle)
    {
        case (ULONG_PTR)BCRYPT_SHA256_ALG_HANDLE: return &KsecPseudoSha256;
        case (ULONG_PTR)BCRYPT_SHA384_ALG_HANDLE: return &KsecPseudoSha384;
        case (ULONG_PTR)BCRYPT_SHA512_ALG_HANDLE: return &KsecPseudoSha512;
        case (ULONG_PTR)BCRYPT_HMAC_SHA256_ALG_HANDLE: return &KsecPseudoHmacSha256;
        case (ULONG_PTR)BCRYPT_HMAC_SHA384_ALG_HANDLE: return &KsecPseudoHmacSha384;
        case (ULONG_PTR)BCRYPT_HMAC_SHA512_ALG_HANDLE: return &KsecPseudoHmacSha512;
        case (ULONG_PTR)BCRYPT_AES_CMAC_ALG_HANDLE: return &KsecPseudoAesCmac;
        case (ULONG_PTR)BCRYPT_AES_CBC_ALG_HANDLE: return &KsecPseudoAesCbc;
        case (ULONG_PTR)BCRYPT_AES_ECB_ALG_HANDLE: return &KsecPseudoAesEcb;
        case (ULONG_PTR)BCRYPT_RNG_ALG_HANDLE: return &KsecPseudoRng;
        case (ULONG_PTR)BCRYPT_RSA_ALG_HANDLE: return &KsecPseudoRsa;
        case (ULONG_PTR)BCRYPT_RSA_SIGN_ALG_HANDLE: return &KsecPseudoRsaSign;
        case (ULONG_PTR)BCRYPT_ECDSA_P256_ALG_HANDLE: return &KsecPseudoEcdsaP256;
        case (ULONG_PTR)BCRYPT_PBKDF2_ALG_HANDLE: return &KsecPseudoPbkdf2;
        case (ULONG_PTR)BCRYPT_SP800108_CTR_HMAC_ALG_HANDLE: return &KsecPseudoSp800108;
        default: return NULL;
    }
}

static KSEC_BCRYPT_ALGORITHM *
KsecGetAlgorithm(
    _In_opt_ BCRYPT_ALG_HANDLE Handle)
{
    const KSEC_BCRYPT_ALGORITHM *Pseudo;
    KSEC_BCRYPT_ALGORITHM *Algorithm = Handle;

    if (Handle == NULL)
        return NULL;
    Pseudo = KsecGetPseudoAlgorithm(Handle);
    if (Pseudo != NULL)
        return (KSEC_BCRYPT_ALGORITHM *)Pseudo;
    if (Algorithm->Header.Magic != KSEC_MAGIC_ALGORITHM)
        return NULL;
    return Algorithm;
}

static KSEC_BCRYPT_HASH *
KsecGetHash(
    _In_opt_ BCRYPT_HASH_HANDLE Handle)
{
    KSEC_BCRYPT_HASH *Hash = Handle;

    return Hash != NULL && Hash->Header.Magic == KSEC_MAGIC_HASH ? Hash : NULL;
}

static KSEC_BCRYPT_KEY *
KsecGetKey(
    _In_opt_ BCRYPT_KEY_HANDLE Handle)
{
    KSEC_BCRYPT_KEY *Key = Handle;

    return Key != NULL && Key->Header.Magic == KSEC_MAGIC_KEY ? Key : NULL;
}

static PVOID
KsecCreateObject(
    _In_ SIZE_T RequiredSize,
    _In_opt_ PUCHAR Object,
    _In_ ULONG ObjectSize,
    _Out_ PULONG Flags)
{
    PVOID Result;

    *Flags = 0;
    if (Object != NULL)
    {
        if (ObjectSize < RequiredSize ||
            ((ULONG_PTR)Object & (TYPE_ALIGNMENT(PVOID) - 1)) != 0)
            return NULL;
        Result = Object;
        *Flags = KSEC_OBJECT_EXTERNAL;
        RtlZeroMemory(Result, RequiredSize);
        return Result;
    }

    return KsecBcryptAllocate(RequiredSize);
}

static VOID
KsecDestroyObject(
    _In_ KSEC_BCRYPT_OBJECT *Object,
    _In_ SIZE_T Size)
{
    ULONG Flags = Object->Flags;

    RtlSecureZeroMemory(Object, Size);
    if (!(Flags & KSEC_OBJECT_EXTERNAL))
        KsecBcryptFree(Object);
}

static NTSTATUS
KsecWriteDwordProperty(
    _Out_writes_bytes_opt_(OutputSize) PUCHAR Output,
    _In_ ULONG OutputSize,
    _Out_ PULONG ResultSize,
    _In_ ULONG Value)
{
    *ResultSize = sizeof(Value);
    if (OutputSize < sizeof(Value))
        return STATUS_BUFFER_TOO_SMALL;
    if (Output != NULL)
        *(PULONG)Output = Value;
    return STATUS_SUCCESS;
}

static NTSTATUS
KsecWriteStringProperty(
    _Out_writes_bytes_opt_(OutputSize) PUCHAR Output,
    _In_ ULONG OutputSize,
    _Out_ PULONG ResultSize,
    _In_ PCWSTR Value)
{
    SIZE_T Size = KsecWideStringBytes(Value);

    if (Size > MAXULONG)
        return STATUS_INTEGER_OVERFLOW;
    *ResultSize = (ULONG)Size;
    if (OutputSize < Size)
        return STATUS_BUFFER_TOO_SMALL;
    if (Output != NULL)
        RtlCopyMemory(Output, Value, Size);
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptOpenAlgorithmProvider(
    BCRYPT_ALG_HANDLE *Handle,
    LPCWSTR AlgorithmId,
    LPCWSTR Implementation,
    ULONG Flags)
{
    KSEC_BCRYPT_ALGORITHM *Algorithm;
    KSEC_BCRYPT_ALGORITHM_ID Id;

    if (Handle == NULL || AlgorithmId == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Implementation != NULL &&
        !KsecEqualWideString(Implementation, MS_PRIMITIVE_PROVIDER))
        return STATUS_NOT_SUPPORTED;
    if (Flags & ~(BCRYPT_ALG_HANDLE_HMAC_FLAG | BCRYPT_HASH_REUSABLE_FLAG))
        return STATUS_NOT_SUPPORTED;

    Id = KsecAlgorithmIdFromName(AlgorithmId);
    if (Id == KsecAlgInvalid)
        return STATUS_NOT_SUPPORTED;
    if ((Flags & BCRYPT_ALG_HANDLE_HMAC_FLAG) && KsecSymCryptHash(Id) == NULL)
        return STATUS_NOT_SUPPORTED;

    Algorithm = KsecBcryptAllocate(sizeof(*Algorithm));
    if (Algorithm == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Algorithm->Header.Magic = KSEC_MAGIC_ALGORITHM;
    Algorithm->Header.Flags = Flags;
    Algorithm->Id = Id;
    Algorithm->ChainMode = KsecChainCbc;
    *Handle = Algorithm;
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptCloseAlgorithmProvider(
    BCRYPT_ALG_HANDLE Handle,
    ULONG Flags)
{
    KSEC_BCRYPT_ALGORITHM *Algorithm;

    if (Flags != 0 || KsecGetPseudoAlgorithm(Handle) != NULL)
        return STATUS_INVALID_HANDLE;
    Algorithm = KsecGetAlgorithm(Handle);
    if (Algorithm == NULL)
        return STATUS_INVALID_HANDLE;
    KsecDestroyObject(&Algorithm->Header, sizeof(*Algorithm));
    return STATUS_SUCCESS;
}

static VOID
KsecResetHash(
    _Inout_ KSEC_BCRYPT_HASH *Hash)
{
    if (Hash->Id == KsecAlgAesCmac)
        SymCryptAesCmacInit(&Hash->State.Cmac.State, &Hash->State.Cmac.Key);
    else if (Hash->Header.Flags & KSEC_HASH_HMAC)
        SymCryptHmacInit(&Hash->State.Hmac.State, &Hash->State.Hmac.Key);
    else
        SymCryptHashInit(Hash->HashAlgorithm, &Hash->State.Hash);
}

NTSTATUS
WINAPI
BCryptCreateHash(
    BCRYPT_ALG_HANDLE AlgorithmHandle,
    BCRYPT_HASH_HANDLE *HashHandle,
    PUCHAR Object,
    ULONG ObjectSize,
    PUCHAR Secret,
    ULONG SecretSize,
    ULONG Flags)
{
    KSEC_BCRYPT_ALGORITHM *Algorithm = KsecGetAlgorithm(AlgorithmHandle);
    KSEC_BCRYPT_HASH *Hash;
    ULONG ObjectFlags;
    SYMCRYPT_ERROR Error;

    if (Algorithm == NULL)
        return STATUS_INVALID_HANDLE;
    if (HashHandle == NULL || (SecretSize != 0 && Secret == NULL))
        return STATUS_INVALID_PARAMETER;
    if (Flags & ~BCRYPT_HASH_REUSABLE_FLAG)
        return STATUS_NOT_SUPPORTED;
    if (Algorithm->Id != KsecAlgAesCmac &&
        KsecSymCryptHash(Algorithm->Id) == NULL)
        return STATUS_NOT_SUPPORTED;

    Hash = KsecCreateObject(sizeof(*Hash), Object, ObjectSize, &ObjectFlags);
    if (Hash == NULL)
        return Object != NULL ? STATUS_BUFFER_TOO_SMALL : STATUS_INSUFFICIENT_RESOURCES;
    Hash->Header.Magic = KSEC_MAGIC_HASH;
    Hash->Header.Flags = ObjectFlags;
    Hash->Id = Algorithm->Id;
    Hash->ResultLength = KsecHashLength(Algorithm->Id);

    if ((Algorithm->Header.Flags | Flags) & BCRYPT_HASH_REUSABLE_FLAG)
        Hash->Header.Flags |= KSEC_HASH_REUSABLE;

    if (Algorithm->Id == KsecAlgAesCmac)
    {
        if (Secret == NULL ||
            (SecretSize != 16 && SecretSize != 24 && SecretSize != 32))
        {
            KsecDestroyObject(&Hash->Header, sizeof(*Hash));
            return STATUS_INVALID_PARAMETER;
        }
        Error = SymCryptAesCmacExpandKey(&Hash->State.Cmac.Key, Secret, SecretSize);
    }
    else
    {
        Hash->HashAlgorithm = KsecSymCryptHash(Algorithm->Id);
        if (Algorithm->Header.Flags & BCRYPT_ALG_HANDLE_HMAC_FLAG)
        {
            Hash->Header.Flags |= KSEC_HASH_HMAC;
            Error = SymCryptHmacExpandKey(Hash->HashAlgorithm,
                                          &Hash->State.Hmac.Key,
                                          Secret,
                                          SecretSize);
        }
        else
        {
            if (SecretSize != 0)
            {
                KsecDestroyObject(&Hash->Header, sizeof(*Hash));
                return STATUS_INVALID_PARAMETER;
            }
            Error = SYMCRYPT_NO_ERROR;
        }
    }

    if (Error != SYMCRYPT_NO_ERROR)
    {
        KsecDestroyObject(&Hash->Header, sizeof(*Hash));
        return STATUS_INVALID_PARAMETER;
    }

    KsecResetHash(Hash);
    *HashHandle = Hash;
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptHashData(
    BCRYPT_HASH_HANDLE Handle,
    PUCHAR Input,
    ULONG InputSize,
    ULONG Flags)
{
    KSEC_BCRYPT_HASH *Hash = KsecGetHash(Handle);

    if (Hash == NULL)
        return STATUS_INVALID_HANDLE;
    if (Flags != 0)
        return STATUS_NOT_SUPPORTED;
    if (InputSize != 0 && Input == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Hash->Id == KsecAlgAesCmac)
        SymCryptAesCmacAppend(&Hash->State.Cmac.State, Input, InputSize);
    else if (Hash->Header.Flags & KSEC_HASH_HMAC)
        SymCryptHmacAppend(&Hash->State.Hmac.State, Input, InputSize);
    else
        SymCryptHashAppend(Hash->HashAlgorithm, &Hash->State.Hash, Input, InputSize);
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptFinishHash(
    BCRYPT_HASH_HANDLE Handle,
    PUCHAR Output,
    ULONG OutputSize,
    ULONG Flags)
{
    KSEC_BCRYPT_HASH *Hash = KsecGetHash(Handle);

    if (Hash == NULL)
        return STATUS_INVALID_HANDLE;
    if (Flags != 0)
        return STATUS_NOT_SUPPORTED;
    if (Output == NULL || OutputSize != Hash->ResultLength)
        return STATUS_INVALID_PARAMETER;

    if (Hash->Id == KsecAlgAesCmac)
        SymCryptAesCmacResult(&Hash->State.Cmac.State, Output);
    else if (Hash->Header.Flags & KSEC_HASH_HMAC)
        SymCryptHmacResult(&Hash->State.Hmac.State, Output);
    else
        SymCryptHashResult(Hash->HashAlgorithm,
                           &Hash->State.Hash,
                           Output,
                           OutputSize);

    if (Hash->Header.Flags & KSEC_HASH_REUSABLE)
        KsecResetHash(Hash);
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptDestroyHash(
    BCRYPT_HASH_HANDLE Handle)
{
    KSEC_BCRYPT_HASH *Hash = KsecGetHash(Handle);

    if (Hash == NULL)
        return STATUS_INVALID_HANDLE;
    KsecDestroyObject(&Hash->Header, sizeof(*Hash));
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptGenRandom(
    BCRYPT_ALG_HANDLE AlgorithmHandle,
    PUCHAR Buffer,
    ULONG BufferSize,
    ULONG Flags)
{
    KSEC_BCRYPT_ALGORITHM *Algorithm;

    if (Buffer == NULL && BufferSize != 0)
        return STATUS_INVALID_PARAMETER;
    if (Flags & ~(BCRYPT_RNG_USE_ENTROPY_IN_BUFFER |
                  BCRYPT_USE_SYSTEM_PREFERRED_RNG))
        return STATUS_NOT_SUPPORTED;

    if (AlgorithmHandle == NULL)
    {
        if (!(Flags & BCRYPT_USE_SYSTEM_PREFERRED_RNG))
            return STATUS_INVALID_HANDLE;
    }
    else
    {
        Algorithm = KsecGetAlgorithm(AlgorithmHandle);
        if (Algorithm == NULL || Algorithm->Id != KsecAlgRng)
            return STATUS_INVALID_HANDLE;
    }

    return KsecGenRandom(Buffer, BufferSize);
}

static VOID
KsecDestroyKeyObject(
    _In_ KSEC_BCRYPT_KEY *Key)
{
    if (Key->Id == KsecAlgPbkdf2 || Key->Id == KsecAlgSp800108)
        KsecBcryptFree(Key->Data.Kdf.Secret);
    else if (Key->Id == KsecAlgRsa || Key->Id == KsecAlgRsaSign)
    {
        if (Key->Data.Rsa.Key != NULL)
            SymCryptRsakeyFree(Key->Data.Rsa.Key);
    }
    else if (Key->Id == KsecAlgEcdsaP256)
    {
        if (Key->Data.Ecc.Key != NULL)
            SymCryptEckeyFree(Key->Data.Ecc.Key);
        if (Key->Data.Ecc.Curve != NULL)
            SymCryptEcurveFree(Key->Data.Ecc.Curve);
    }
    KsecDestroyObject(&Key->Header, sizeof(*Key));
}

NTSTATUS
WINAPI
BCryptGenerateSymmetricKey(
    BCRYPT_ALG_HANDLE AlgorithmHandle,
    BCRYPT_KEY_HANDLE *KeyHandle,
    PUCHAR Object,
    ULONG ObjectSize,
    PUCHAR Secret,
    ULONG SecretSize,
    ULONG Flags)
{
    KSEC_BCRYPT_ALGORITHM *Algorithm = KsecGetAlgorithm(AlgorithmHandle);
    KSEC_BCRYPT_KEY *Key;
    ULONG ObjectFlags;
    SYMCRYPT_ERROR Error;

    if (Algorithm == NULL)
        return STATUS_INVALID_HANDLE;
    if (KeyHandle == NULL || Secret == NULL || SecretSize == 0)
        return STATUS_INVALID_PARAMETER;
    if (Flags != 0)
        return STATUS_NOT_SUPPORTED;
    if (Algorithm->Id != KsecAlgAes &&
        Algorithm->Id != KsecAlgPbkdf2 &&
        Algorithm->Id != KsecAlgSp800108)
        return STATUS_NOT_SUPPORTED;

    Key = KsecCreateObject(sizeof(*Key), Object, ObjectSize, &ObjectFlags);
    if (Key == NULL)
        return Object != NULL ? STATUS_BUFFER_TOO_SMALL : STATUS_INSUFFICIENT_RESOURCES;
    Key->Header.Magic = KSEC_MAGIC_KEY;
    Key->Header.Flags = ObjectFlags;
    Key->Id = Algorithm->Id;
    Key->ChainMode = Algorithm->ChainMode;
    ExInitializeFastMutex(&Key->Lock);

    if (Algorithm->Id == KsecAlgAes)
    {
        if (SecretSize != 16 && SecretSize != 24 && SecretSize != 32)
        {
            KsecDestroyKeyObject(Key);
            return STATUS_INVALID_PARAMETER;
        }
        Error = SymCryptAesExpandKey(&Key->Data.Aes.ExpandedKey,
                                     Secret,
                                     SecretSize);
        if (Error != SYMCRYPT_NO_ERROR)
        {
            KsecDestroyKeyObject(Key);
            return STATUS_INVALID_PARAMETER;
        }
        Key->BitLength = SecretSize * 8;
    }
    else
    {
        Key->Data.Kdf.Secret = KsecBcryptAllocate(SecretSize);
        if (Key->Data.Kdf.Secret == NULL)
        {
            KsecDestroyKeyObject(Key);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(Key->Data.Kdf.Secret, Secret, SecretSize);
        Key->Data.Kdf.SecretLength = SecretSize;
        Key->BitLength = SecretSize * 8;
    }

    *KeyHandle = Key;
    return STATUS_SUCCESS;
}

static NTSTATUS
KsecDecryptAes(
    _Inout_ KSEC_BCRYPT_KEY *Key,
    _In_reads_bytes_(InputSize) PUCHAR Input,
    _In_ ULONG InputSize,
    _Inout_updates_bytes_opt_(IvSize) PUCHAR Iv,
    _In_ ULONG IvSize,
    _Out_writes_bytes_opt_(OutputSize) PUCHAR Output,
    _In_ ULONG OutputSize,
    _Out_ PULONG ResultSize,
    _In_ ULONG Flags)
{
    UCHAR ChainingValue[KSEC_AES_BLOCK_SIZE];
    UCHAR FinalBlock[KSEC_AES_BLOCK_SIZE];
    ULONG PrefixSize = InputSize;
    ULONG PaddingSize = 0;
    ULONG Index;

    if (Input == NULL || ResultSize == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Flags & ~BCRYPT_BLOCK_PADDING)
        return STATUS_NOT_SUPPORTED;
    if ((InputSize % KSEC_AES_BLOCK_SIZE) != 0)
        return STATUS_INVALID_BUFFER_SIZE;
    if (Key->ChainMode == KsecChainEcb && Iv != NULL)
        return STATUS_INVALID_PARAMETER;
    if (Key->ChainMode == KsecChainCbc && Iv != NULL && IvSize != KSEC_AES_BLOCK_SIZE)
        return STATUS_INVALID_PARAMETER;

    *ResultSize = InputSize;
    if (Output == NULL)
        return STATUS_SUCCESS;
    if (Flags & BCRYPT_BLOCK_PADDING)
    {
        if (InputSize == 0)
            return STATUS_INVALID_BUFFER_SIZE;
        PrefixSize -= KSEC_AES_BLOCK_SIZE;
        if (OutputSize + KSEC_AES_BLOCK_SIZE < InputSize)
            return STATUS_BUFFER_TOO_SMALL;
    }
    else if (OutputSize < InputSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    ExAcquireFastMutex(&Key->Lock);
    if (Iv != NULL)
        RtlCopyMemory(ChainingValue, Iv, sizeof(ChainingValue));
    else
        RtlCopyMemory(ChainingValue, Key->Data.Aes.Vector, sizeof(ChainingValue));

    if (PrefixSize != 0)
    {
        if (Key->ChainMode == KsecChainEcb)
            SymCryptAesEcbDecrypt(&Key->Data.Aes.ExpandedKey,
                                  Input,
                                  Output,
                                  PrefixSize);
        else
            SymCryptAesCbcDecrypt(&Key->Data.Aes.ExpandedKey,
                                  ChainingValue,
                                  Input,
                                  Output,
                                  PrefixSize);
    }

    if (Flags & BCRYPT_BLOCK_PADDING)
    {
        if (Key->ChainMode == KsecChainEcb)
            SymCryptAesDecrypt(&Key->Data.Aes.ExpandedKey,
                               Input + PrefixSize,
                               FinalBlock);
        else
            SymCryptAesCbcDecrypt(&Key->Data.Aes.ExpandedKey,
                                  ChainingValue,
                                  Input + PrefixSize,
                                  FinalBlock,
                                  sizeof(FinalBlock));

        PaddingSize = FinalBlock[KSEC_AES_BLOCK_SIZE - 1];
        if (PaddingSize == 0 || PaddingSize > KSEC_AES_BLOCK_SIZE)
        {
            ExReleaseFastMutex(&Key->Lock);
            RtlSecureZeroMemory(FinalBlock, sizeof(FinalBlock));
            return STATUS_UNSUCCESSFUL;
        }
        for (Index = 0; Index < PaddingSize; ++Index)
        {
            if (FinalBlock[KSEC_AES_BLOCK_SIZE - 1 - Index] != PaddingSize)
            {
                ExReleaseFastMutex(&Key->Lock);
                RtlSecureZeroMemory(FinalBlock, sizeof(FinalBlock));
                return STATUS_UNSUCCESSFUL;
            }
        }
        *ResultSize = InputSize - PaddingSize;
        if (OutputSize < *ResultSize)
        {
            ExReleaseFastMutex(&Key->Lock);
            RtlSecureZeroMemory(FinalBlock, sizeof(FinalBlock));
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlCopyMemory(Output + PrefixSize,
                      FinalBlock,
                      KSEC_AES_BLOCK_SIZE - PaddingSize);
    }

    if (Key->ChainMode == KsecChainCbc)
    {
        RtlCopyMemory(Key->Data.Aes.Vector, ChainingValue, sizeof(ChainingValue));
        if (Iv != NULL)
            RtlCopyMemory(Iv, ChainingValue, sizeof(ChainingValue));
    }
    ExReleaseFastMutex(&Key->Lock);
    RtlSecureZeroMemory(FinalBlock, sizeof(FinalBlock));
    RtlSecureZeroMemory(ChainingValue, sizeof(ChainingValue));
    return STATUS_SUCCESS;
}

static PCSYMCRYPT_HASH
KsecHashFromWideString(
    _In_opt_ PCWSTR Name)
{
    if (KsecEqualWideString(Name, BCRYPT_SHA256_ALGORITHM))
        return SymCryptSha256Algorithm;
    if (KsecEqualWideString(Name, BCRYPT_SHA384_ALGORITHM))
        return SymCryptSha384Algorithm;
    if (KsecEqualWideString(Name, BCRYPT_SHA512_ALGORITHM))
        return SymCryptSha512Algorithm;
    return NULL;
}

static NTSTATUS
KsecDecryptRsa(
    _In_ KSEC_BCRYPT_KEY *Key,
    _In_reads_bytes_(InputSize) PUCHAR Input,
    _In_ ULONG InputSize,
    _In_opt_ PVOID Padding,
    _Out_writes_bytes_opt_(OutputSize) PUCHAR Output,
    _In_ ULONG OutputSize,
    _Out_ PULONG ResultSize,
    _In_ ULONG Flags)
{
    SIZE_T Size = Key->BitLength / 8;
    SYMCRYPT_ERROR Error;

    if (!(Key->Header.Flags & KSEC_KEY_PRIVATE))
        return STATUS_INVALID_HANDLE;
    if (Input == NULL || ResultSize == NULL || InputSize != Size)
        return STATUS_INVALID_PARAMETER;
    if (Output == NULL)
    {
        *ResultSize = (ULONG)Size;
        return STATUS_SUCCESS;
    }

    if (Flags == 0 || Flags == BCRYPT_PAD_NONE)
    {
        if (OutputSize < Size)
            return STATUS_BUFFER_TOO_SMALL;
        Error = SymCryptRsaRawDecrypt(Key->Data.Rsa.Key,
                                      Input,
                                      InputSize,
                                      SYMCRYPT_NUMBER_FORMAT_MSB_FIRST,
                                      0,
                                      Output,
                                      OutputSize);
        *ResultSize = (ULONG)Size;
    }
    else if (Flags == BCRYPT_PAD_PKCS1)
    {
        Error = SymCryptRsaPkcs1Decrypt(Key->Data.Rsa.Key,
                                        Input,
                                        InputSize,
                                        SYMCRYPT_NUMBER_FORMAT_MSB_FIRST,
                                        0,
                                        Output,
                                        OutputSize,
                                        &Size);
        *ResultSize = (ULONG)Size;
    }
    else if (Flags == BCRYPT_PAD_OAEP)
    {
        BCRYPT_OAEP_PADDING_INFO *Info = Padding;
        PCSYMCRYPT_HASH HashAlgorithm;

        if (Info == NULL ||
            (HashAlgorithm = KsecHashFromWideString(Info->pszAlgId)) == NULL)
            return STATUS_INVALID_PARAMETER;
        Error = SymCryptRsaOaepDecrypt(Key->Data.Rsa.Key,
                                       Input,
                                       InputSize,
                                       SYMCRYPT_NUMBER_FORMAT_MSB_FIRST,
                                       HashAlgorithm,
                                       Info->pbLabel,
                                       Info->cbLabel,
                                       0,
                                       Output,
                                       OutputSize,
                                       &Size);
        *ResultSize = (ULONG)Size;
    }
    else
    {
        return STATUS_NOT_SUPPORTED;
    }

    return Error == SYMCRYPT_NO_ERROR ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS
WINAPI
BCryptDecrypt(
    BCRYPT_KEY_HANDLE Handle,
    PUCHAR Input,
    ULONG InputSize,
    PVOID Padding,
    PUCHAR Iv,
    ULONG IvSize,
    PUCHAR Output,
    ULONG OutputSize,
    PULONG ResultSize,
    ULONG Flags)
{
    KSEC_BCRYPT_KEY *Key = KsecGetKey(Handle);

    if (Key == NULL)
        return STATUS_INVALID_HANDLE;
    if (Key->Id == KsecAlgAes)
        return KsecDecryptAes(Key, Input, InputSize, Iv, IvSize,
                              Output, OutputSize, ResultSize, Flags);
    if (Key->Id == KsecAlgRsa)
        return KsecDecryptRsa(Key, Input, InputSize, Padding,
                              Output, OutputSize, ResultSize, Flags);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
KsecImportRsaKey(
    _In_ KSEC_BCRYPT_ALGORITHM *Algorithm,
    _In_reads_bytes_(InputSize) PUCHAR Input,
    _In_ ULONG InputSize,
    _Out_ BCRYPT_KEY_HANDLE *KeyHandle)
{
    BCRYPT_RSAKEY_BLOB *Blob = (BCRYPT_RSAKEY_BLOB *)Input;
    KSEC_BCRYPT_KEY *Key;
    const UCHAR *Exponent;
    const UCHAR *Modulus;
    const UCHAR *Primes[2] = {NULL, NULL};
    SIZE_T PrimeSizes[2] = {0, 0};
    SIZE_T RequiredSize;
    SYMCRYPT_RSA_PARAMS Parameters;
    UINT64 PublicExponent = 0;
    UINT32 ImportFlags;
    UINT32 PrimeCount = 0;
    ULONG Index;
    SYMCRYPT_ERROR Error;

    if (InputSize < sizeof(*Blob))
        return STATUS_INVALID_PARAMETER;
    if (Blob->Magic != BCRYPT_RSAPUBLIC_MAGIC &&
        Blob->Magic != BCRYPT_RSAPRIVATE_MAGIC &&
        Blob->Magic != BCRYPT_RSAFULLPRIVATE_MAGIC)
        return STATUS_NOT_SUPPORTED;
    if (Blob->cbPublicExp == 0 || Blob->cbPublicExp > sizeof(PublicExponent) ||
        Blob->cbModulus == 0 || Blob->BitLength != Blob->cbModulus * 8)
        return STATUS_INVALID_PARAMETER;

    RequiredSize = sizeof(*Blob) + Blob->cbPublicExp + Blob->cbModulus;
    if (Blob->Magic != BCRYPT_RSAPUBLIC_MAGIC)
        RequiredSize += Blob->cbPrime1 + Blob->cbPrime2;
    if (Blob->Magic == BCRYPT_RSAFULLPRIVATE_MAGIC)
        RequiredSize += Blob->cbPrime1 * 2 + Blob->cbPrime2 + Blob->cbModulus;
    if (RequiredSize != InputSize)
        return STATUS_INVALID_PARAMETER;

    Exponent = (const UCHAR *)(Blob + 1);
    for (Index = 0; Index < Blob->cbPublicExp; ++Index)
        PublicExponent = (PublicExponent << 8) | Exponent[Index];
    if (PublicExponent <= 1)
        return STATUS_INVALID_PARAMETER;
    Modulus = Exponent + Blob->cbPublicExp;

    Key = KsecBcryptAllocate(sizeof(*Key));
    if (Key == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Key->Header.Magic = KSEC_MAGIC_KEY;
    Key->Id = Algorithm->Id;
    Key->BitLength = Blob->BitLength;
    ExInitializeFastMutex(&Key->Lock);

    if (Blob->Magic != BCRYPT_RSAPUBLIC_MAGIC)
    {
        Key->Header.Flags |= KSEC_KEY_PRIVATE;
        PrimeCount = 2;
        Primes[0] = Modulus + Blob->cbModulus;
        Primes[1] = Primes[0] + Blob->cbPrime1;
        PrimeSizes[0] = Blob->cbPrime1;
        PrimeSizes[1] = Blob->cbPrime2;
    }

    Parameters.version = 1;
    Parameters.nBitsOfModulus = Blob->BitLength;
    Parameters.nPrimes = PrimeCount;
    Parameters.nPubExp = 1;
    Key->Data.Rsa.Key = SymCryptRsakeyAllocate(&Parameters, 0);
    if (Key->Data.Rsa.Key == NULL)
    {
        KsecDestroyKeyObject(Key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ImportFlags = SYMCRYPT_FLAG_KEY_NO_FIPS | SYMCRYPT_FLAG_KEY_MINIMAL_VALIDATION |
                  SYMCRYPT_FLAG_RSAKEY_SIGN;
    if (Algorithm->Id == KsecAlgRsa)
        ImportFlags |= SYMCRYPT_FLAG_RSAKEY_ENCRYPT;
    Error = SymCryptRsakeySetValue(Modulus,
                                    Blob->cbModulus,
                                    &PublicExponent,
                                    1,
                                    Primes,
                                    PrimeSizes,
                                    PrimeCount,
                                    SYMCRYPT_NUMBER_FORMAT_MSB_FIRST,
                                    ImportFlags,
                                    Key->Data.Rsa.Key);
    if (Error != SYMCRYPT_NO_ERROR)
    {
        KsecDestroyKeyObject(Key);
        return STATUS_INVALID_PARAMETER;
    }

    *KeyHandle = Key;
    return STATUS_SUCCESS;
}

static NTSTATUS
KsecImportEcdsaP256Key(
    _In_reads_bytes_(InputSize) PUCHAR Input,
    _In_ ULONG InputSize,
    _Out_ BCRYPT_KEY_HANDLE *KeyHandle)
{
    BCRYPT_ECCKEY_BLOB *Blob = (BCRYPT_ECCKEY_BLOB *)Input;
    KSEC_BCRYPT_KEY *Key;
    PUCHAR PublicKey;
    PUCHAR PrivateKey = NULL;
    ULONG RequiredSize;
    SYMCRYPT_ERROR Error;

    if (InputSize < sizeof(*Blob) || Blob->cbKey != 32)
        return STATUS_INVALID_PARAMETER;
    if (Blob->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC &&
        Blob->dwMagic != BCRYPT_ECDSA_PRIVATE_P256_MAGIC)
        return STATUS_NOT_SUPPORTED;

    RequiredSize = sizeof(*Blob) + Blob->cbKey * 2;
    if (Blob->dwMagic == BCRYPT_ECDSA_PRIVATE_P256_MAGIC)
        RequiredSize += Blob->cbKey;
    if (InputSize != RequiredSize)
        return STATUS_INVALID_PARAMETER;

    Key = KsecBcryptAllocate(sizeof(*Key));
    if (Key == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Key->Header.Magic = KSEC_MAGIC_KEY;
    Key->Id = KsecAlgEcdsaP256;
    Key->BitLength = 256;
    ExInitializeFastMutex(&Key->Lock);

    Key->Data.Ecc.Curve = SymCryptEcurveAllocate(SymCryptEcurveParamsNistP256, 0);
    if (Key->Data.Ecc.Curve != NULL)
        Key->Data.Ecc.Key = SymCryptEckeyAllocate(Key->Data.Ecc.Curve);
    if (Key->Data.Ecc.Curve == NULL || Key->Data.Ecc.Key == NULL)
    {
        KsecDestroyKeyObject(Key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PublicKey = (PUCHAR)(Blob + 1);
    if (Blob->dwMagic == BCRYPT_ECDSA_PRIVATE_P256_MAGIC)
    {
        Key->Header.Flags |= KSEC_KEY_PRIVATE;
        PrivateKey = PublicKey + Blob->cbKey * 2;
    }
    Error = SymCryptEckeySetValue(PrivateKey,
                                   PrivateKey != NULL ? Blob->cbKey : 0,
                                   PublicKey,
                                   Blob->cbKey * 2,
                                   SYMCRYPT_NUMBER_FORMAT_MSB_FIRST,
                                   SYMCRYPT_ECPOINT_FORMAT_XY,
                                   SYMCRYPT_FLAG_KEY_NO_FIPS |
                                   SYMCRYPT_FLAG_KEY_MINIMAL_VALIDATION |
                                   SYMCRYPT_FLAG_ECKEY_ECDSA,
                                   Key->Data.Ecc.Key);
    if (Error != SYMCRYPT_NO_ERROR)
    {
        KsecDestroyKeyObject(Key);
        return STATUS_INVALID_PARAMETER;
    }

    *KeyHandle = Key;
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptImportKeyPair(
    BCRYPT_ALG_HANDLE AlgorithmHandle,
    BCRYPT_KEY_HANDLE DecryptKey,
    LPCWSTR BlobType,
    BCRYPT_KEY_HANDLE *KeyHandle,
    UCHAR *Input,
    ULONG InputSize,
    ULONG Flags)
{
    KSEC_BCRYPT_ALGORITHM *Algorithm = KsecGetAlgorithm(AlgorithmHandle);

    if (Algorithm == NULL)
        return STATUS_INVALID_HANDLE;
    if (DecryptKey != NULL || (Flags & ~BCRYPT_NO_KEY_VALIDATION) != 0)
        return STATUS_NOT_SUPPORTED;
    if (BlobType == NULL || KeyHandle == NULL || Input == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Algorithm->Id == KsecAlgRsa || Algorithm->Id == KsecAlgRsaSign)
    {
        if (!KsecEqualWideString(BlobType, BCRYPT_RSAPUBLIC_BLOB) &&
            !KsecEqualWideString(BlobType, BCRYPT_RSAPRIVATE_BLOB) &&
            !KsecEqualWideString(BlobType, BCRYPT_RSAFULLPRIVATE_BLOB) &&
            !KsecEqualWideString(BlobType, BCRYPT_PUBLIC_KEY_BLOB))
            return STATUS_NOT_SUPPORTED;
        return KsecImportRsaKey(Algorithm, Input, InputSize, KeyHandle);
    }
    if (Algorithm->Id == KsecAlgEcdsaP256)
    {
        if (!KsecEqualWideString(BlobType, BCRYPT_ECCPUBLIC_BLOB) &&
            !KsecEqualWideString(BlobType, BCRYPT_ECCPRIVATE_BLOB) &&
            !KsecEqualWideString(BlobType, BCRYPT_PUBLIC_KEY_BLOB))
            return STATUS_NOT_SUPPORTED;
        return KsecImportEcdsaP256Key(Input, InputSize, KeyHandle);
    }
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
KsecGetRsaOid(
    _In_opt_ PCWSTR HashName,
    _Out_ PCSYMCRYPT_OID *Oids,
    _Out_ SIZE_T *OidCount)
{
    if (HashName == NULL)
    {
        *Oids = NULL;
        *OidCount = 0;
        return STATUS_SUCCESS;
    }
    if (KsecEqualWideString(HashName, BCRYPT_SHA256_ALGORITHM))
        *Oids = SymCryptSha256OidList;
    else if (KsecEqualWideString(HashName, BCRYPT_SHA384_ALGORITHM))
        *Oids = SymCryptSha384OidList;
    else if (KsecEqualWideString(HashName, BCRYPT_SHA512_ALGORITHM))
        *Oids = SymCryptSha512OidList;
    else
        return STATUS_NOT_SUPPORTED;
    *OidCount = 2;
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptVerifySignature(
    BCRYPT_KEY_HANDLE Handle,
    PVOID Padding,
    UCHAR *Hash,
    ULONG HashSize,
    UCHAR *Signature,
    ULONG SignatureSize,
    ULONG Flags)
{
    KSEC_BCRYPT_KEY *Key = KsecGetKey(Handle);
    SYMCRYPT_ERROR Error;

    if (Key == NULL)
        return STATUS_INVALID_HANDLE;
    if (Hash == NULL || HashSize == 0 || Signature == NULL || SignatureSize == 0)
        return STATUS_INVALID_PARAMETER;

    if (Key->Id == KsecAlgEcdsaP256)
    {
        if (Padding != NULL || Flags != 0 || SignatureSize != 64)
            return STATUS_INVALID_PARAMETER;
        Error = SymCryptEcDsaVerify(Key->Data.Ecc.Key,
                                    Hash,
                                    HashSize,
                                    Signature,
                                    SignatureSize,
                                    SYMCRYPT_NUMBER_FORMAT_MSB_FIRST,
                                    0);
    }
    else if (Key->Id == KsecAlgRsa || Key->Id == KsecAlgRsaSign)
    {
        if (SignatureSize != Key->BitLength / 8 || Padding == NULL)
            return STATUS_INVALID_PARAMETER;
        if (Flags == BCRYPT_PAD_PKCS1)
        {
            BCRYPT_PKCS1_PADDING_INFO *Info = Padding;
            PCSYMCRYPT_OID Oids;
            SIZE_T OidCount;
            NTSTATUS Status = KsecGetRsaOid(Info->pszAlgId, &Oids, &OidCount);
            ULONG VerifyFlags = Oids == NULL ?
                                SYMCRYPT_FLAG_RSA_PKCS1_OPTIONAL_HASH_OID : 0;

            if (!NT_SUCCESS(Status))
                return Status;
            Error = SymCryptRsaPkcs1Verify(Key->Data.Rsa.Key,
                                            Hash,
                                            HashSize,
                                            Signature,
                                            SignatureSize,
                                            SYMCRYPT_NUMBER_FORMAT_MSB_FIRST,
                                            Oids,
                                            OidCount,
                                            VerifyFlags);
        }
        else if (Flags == BCRYPT_PAD_PSS)
        {
            BCRYPT_PSS_PADDING_INFO *Info = Padding;
            PCSYMCRYPT_HASH HashAlgorithm = KsecHashFromWideString(Info->pszAlgId);

            if (HashAlgorithm == NULL)
                return STATUS_NOT_SUPPORTED;
            Error = SymCryptRsaPssVerify(Key->Data.Rsa.Key,
                                          Hash,
                                          HashSize,
                                          Signature,
                                          SignatureSize,
                                          SYMCRYPT_NUMBER_FORMAT_MSB_FIRST,
                                          HashAlgorithm,
                                          Info->cbSalt,
                                          0);
        }
        else
        {
            return STATUS_NOT_SUPPORTED;
        }
    }
    else
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Error == SYMCRYPT_SIGNATURE_VERIFICATION_FAILURE)
        return STATUS_INVALID_SIGNATURE;
    return Error == SYMCRYPT_NO_ERROR ? STATUS_SUCCESS : STATUS_INTERNAL_ERROR;
}

NTSTATUS
WINAPI
BCryptDestroyKey(
    BCRYPT_KEY_HANDLE Handle)
{
    KSEC_BCRYPT_KEY *Key = KsecGetKey(Handle);

    if (Key == NULL)
        return STATUS_INVALID_HANDLE;
    KsecDestroyKeyObject(Key);
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptKeyDerivation(
    BCRYPT_KEY_HANDLE Handle,
    BCryptBufferDesc *Description,
    UCHAR *Output,
    ULONG OutputSize,
    ULONG *ResultSize,
    ULONG Flags)
{
    KSEC_BCRYPT_KEY *Key = KsecGetKey(Handle);
    PCSYMCRYPT_MAC Mac = NULL;
    PUCHAR Salt = NULL;
    ULONG SaltSize = 0;
    PUCHAR Label = NULL;
    ULONG LabelSize = 0;
    PUCHAR Context = NULL;
    ULONG ContextSize = 0;
    UINT64 Iterations = 10000;
    ULONG Index;
    SYMCRYPT_ERROR Error;

    if (Key == NULL)
        return STATUS_INVALID_HANDLE;
    if (Description == NULL || ResultSize == NULL ||
        Description->ulVersion != BCRYPTBUFFER_VERSION ||
        (Description->cBuffers != 0 && Description->pBuffers == NULL) ||
        (OutputSize != 0 && Output == NULL))
        return STATUS_INVALID_PARAMETER;
    if (Flags != 0)
        return STATUS_NOT_SUPPORTED;
    if (Key->Id != KsecAlgPbkdf2 && Key->Id != KsecAlgSp800108)
        return STATUS_NOT_SUPPORTED;

    for (Index = 0; Index < Description->cBuffers; ++Index)
    {
        BCryptBuffer *Buffer = &Description->pBuffers[Index];

        if (Buffer->cbBuffer != 0 && Buffer->pvBuffer == NULL)
            return STATUS_INVALID_PARAMETER;
        switch (Buffer->BufferType)
        {
            case KDF_HASH_ALGORITHM:
                Mac = KsecSymCryptMacFromName(Buffer->pvBuffer, Buffer->cbBuffer);
                break;
            case KDF_SALT:
                Salt = Buffer->pvBuffer;
                SaltSize = Buffer->cbBuffer;
                break;
            case KDF_ITERATION_COUNT:
                if (Buffer->cbBuffer != sizeof(Iterations))
                    return STATUS_INVALID_PARAMETER;
                Iterations = *(UINT64 *)Buffer->pvBuffer;
                break;
            case KDF_LABEL:
                Label = Buffer->pvBuffer;
                LabelSize = Buffer->cbBuffer;
                break;
            case KDF_CONTEXT:
                Context = Buffer->pvBuffer;
                ContextSize = Buffer->cbBuffer;
                break;
            default:
                return STATUS_NOT_SUPPORTED;
        }
    }
    if (Mac == NULL || OutputSize == 0)
        return STATUS_INVALID_PARAMETER;

    if (Key->Id == KsecAlgPbkdf2)
    {
        if (Iterations == 0 || Label != NULL || Context != NULL)
            return STATUS_INVALID_PARAMETER;
        Error = SymCryptPbkdf2(Mac,
                                Key->Data.Kdf.Secret,
                                Key->Data.Kdf.SecretLength,
                                Salt,
                                SaltSize,
                                Iterations,
                                Output,
                                OutputSize);
    }
    else
    {
        if (Salt != NULL)
            return STATUS_INVALID_PARAMETER;
        Error = SymCryptSp800_108(Mac,
                                   Key->Data.Kdf.Secret,
                                   Key->Data.Kdf.SecretLength,
                                   Label,
                                   LabelSize,
                                   Context,
                                   ContextSize,
                                   Output,
                                   OutputSize);
    }

    if (Error != SYMCRYPT_NO_ERROR)
        return STATUS_INTERNAL_ERROR;
    *ResultSize = OutputSize;
    return STATUS_SUCCESS;
}

NTSTATUS
WINAPI
BCryptGetProperty(
    BCRYPT_HANDLE Handle,
    LPCWSTR Property,
    PUCHAR Output,
    ULONG OutputSize,
    ULONG *ResultSize,
    ULONG Flags)
{
    KSEC_BCRYPT_ALGORITHM *Algorithm;
    KSEC_BCRYPT_OBJECT *Object = Handle;
    KSEC_BCRYPT_ALGORITHM_ID Id;
    ULONG Value;

    if (Handle == NULL)
        return STATUS_INVALID_HANDLE;
    if (Property == NULL || ResultSize == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Flags != 0)
        return STATUS_NOT_SUPPORTED;

    Algorithm = KsecGetAlgorithm(Handle);
    if (Algorithm != NULL)
    {
        Id = Algorithm->Id;
        if (KsecEqualWideString(Property, BCRYPT_ALGORITHM_NAME))
            return KsecWriteStringProperty(Output, OutputSize, ResultSize,
                                           KsecAlgorithmName(Id));
        if (KsecEqualWideString(Property, BCRYPT_OBJECT_LENGTH))
        {
            Value = KsecHashLength(Id) != 0 ? sizeof(KSEC_BCRYPT_HASH) :
                    sizeof(KSEC_BCRYPT_KEY);
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize, Value);
        }
        if (KsecEqualWideString(Property, BCRYPT_KEY_OBJECT_LENGTH))
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize,
                                          sizeof(KSEC_BCRYPT_KEY));
        if (KsecEqualWideString(Property, BCRYPT_HASH_LENGTH) &&
            (Value = KsecHashLength(Id)) != 0)
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize, Value);
        if (KsecEqualWideString(Property, BCRYPT_HASH_BLOCK_LENGTH) &&
            (Value = KsecHashBlockLength(Id)) != 0)
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize, Value);
        if (Id == KsecAlgAes &&
            KsecEqualWideString(Property, BCRYPT_BLOCK_LENGTH))
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize,
                                          KSEC_AES_BLOCK_SIZE);
        if (Id == KsecAlgAes &&
            KsecEqualWideString(Property, BCRYPT_KEY_LENGTHS))
        {
            BCRYPT_KEY_LENGTHS_STRUCT Lengths = {128, 256, 64};

            *ResultSize = sizeof(Lengths);
            if (OutputSize < sizeof(Lengths))
                return STATUS_BUFFER_TOO_SMALL;
            if (Output != NULL)
                RtlCopyMemory(Output, &Lengths, sizeof(Lengths));
            return STATUS_SUCCESS;
        }
        if (Id == KsecAlgAes &&
            KsecEqualWideString(Property, BCRYPT_CHAINING_MODE))
            return KsecWriteStringProperty(Output, OutputSize, ResultSize,
                                           Algorithm->ChainMode == KsecChainEcb ?
                                           BCRYPT_CHAIN_MODE_ECB : BCRYPT_CHAIN_MODE_CBC);
        return STATUS_NOT_SUPPORTED;
    }

    if (Object->Magic == KSEC_MAGIC_HASH)
    {
        KSEC_BCRYPT_HASH *Hash = (KSEC_BCRYPT_HASH *)Object;

        if (KsecEqualWideString(Property, BCRYPT_HASH_LENGTH))
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize,
                                          Hash->ResultLength);
        if (KsecEqualWideString(Property, BCRYPT_HASH_BLOCK_LENGTH))
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize,
                                          KsecHashBlockLength(Hash->Id));
        return STATUS_NOT_SUPPORTED;
    }

    if (Object->Magic == KSEC_MAGIC_KEY)
    {
        KSEC_BCRYPT_KEY *Key = (KSEC_BCRYPT_KEY *)Object;

        if (KsecEqualWideString(Property, BCRYPT_KEY_STRENGTH) ||
            KsecEqualWideString(Property, BCRYPT_KEY_LENGTH))
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize,
                                          Key->BitLength);
        if ((Key->Id == KsecAlgRsa || Key->Id == KsecAlgRsaSign) &&
            KsecEqualWideString(Property, BCRYPT_SIGNATURE_LENGTH))
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize,
                                          Key->BitLength / 8);
        if (Key->Id == KsecAlgAes &&
            KsecEqualWideString(Property, BCRYPT_BLOCK_LENGTH))
            return KsecWriteDwordProperty(Output, OutputSize, ResultSize,
                                          KSEC_AES_BLOCK_SIZE);
        if (Key->Id == KsecAlgAes &&
            KsecEqualWideString(Property, BCRYPT_CHAINING_MODE))
            return KsecWriteStringProperty(Output, OutputSize, ResultSize,
                                           Key->ChainMode == KsecChainEcb ?
                                           BCRYPT_CHAIN_MODE_ECB : BCRYPT_CHAIN_MODE_CBC);
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_INVALID_HANDLE;
}

NTSTATUS
WINAPI
BCryptSetProperty(
    BCRYPT_HANDLE Handle,
    LPCWSTR Property,
    PUCHAR Input,
    ULONG InputSize,
    ULONG Flags)
{
    KSEC_BCRYPT_ALGORITHM *Algorithm;
    KSEC_BCRYPT_KEY *Key;
    KSEC_BCRYPT_CHAIN_MODE Mode;

    if (Handle == NULL)
        return STATUS_INVALID_HANDLE;
    if (Property == NULL || Input == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Flags != 0)
        return STATUS_NOT_SUPPORTED;
    if (!KsecEqualWideString(Property, BCRYPT_CHAINING_MODE))
        return STATUS_NOT_SUPPORTED;

    if (KsecEqualWideBuffer((PCWSTR)Input, InputSize, BCRYPT_CHAIN_MODE_CBC))
        Mode = KsecChainCbc;
    else if (KsecEqualWideBuffer((PCWSTR)Input, InputSize, BCRYPT_CHAIN_MODE_ECB))
        Mode = KsecChainEcb;
    else
        return STATUS_NOT_SUPPORTED;

    Algorithm = KsecGetAlgorithm(Handle);
    if (Algorithm != NULL)
    {
        if (KsecGetPseudoAlgorithm(Handle) != NULL)
            return STATUS_ACCESS_DENIED;
        if (Algorithm->Id != KsecAlgAes)
            return STATUS_NOT_SUPPORTED;
        Algorithm->ChainMode = Mode;
        return STATUS_SUCCESS;
    }

    Key = KsecGetKey(Handle);
    if (Key == NULL)
        return STATUS_INVALID_HANDLE;
    if (Key->Id != KsecAlgAes)
        return STATUS_NOT_SUPPORTED;
    Key->ChainMode = Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS
KsecBcryptHashKnownAnswer(
    _In_ PCWSTR AlgorithmName,
    _In_ ULONG AlgorithmFlags,
    _In_reads_bytes_opt_(SecretSize) const UCHAR *Secret,
    _In_ ULONG SecretSize,
    _In_reads_bytes_opt_(InputSize) const UCHAR *Input,
    _In_ ULONG InputSize,
    _In_reads_bytes_(ExpectedSize) const UCHAR *Expected,
    _In_ ULONG ExpectedSize)
{
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_HASH_HANDLE Hash = NULL;
    UCHAR Output[SYMCRYPT_SHA512_RESULT_SIZE];
    NTSTATUS Status;

    if (ExpectedSize > sizeof(Output))
        return STATUS_INVALID_BUFFER_SIZE;

    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         AlgorithmName,
                                         NULL,
                                         AlgorithmFlags);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = BCryptCreateHash(Algorithm,
                              &Hash,
                              NULL,
                              0,
                              (PUCHAR)Secret,
                              SecretSize,
                              0);
    if (NT_SUCCESS(Status))
        Status = BCryptHashData(Hash, (PUCHAR)Input, InputSize, 0);
    if (NT_SUCCESS(Status))
        Status = BCryptFinishHash(Hash, Output, ExpectedSize, 0);
    if (NT_SUCCESS(Status) &&
        RtlCompareMemory(Output, Expected, ExpectedSize) != ExpectedSize)
    {
        Status = STATUS_DATA_ERROR;
    }

    if (Hash != NULL)
        BCryptDestroyHash(Hash);
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    RtlSecureZeroMemory(Output, sizeof(Output));
    return Status;
}

NTSTATUS
NTAPI
KsecInitializeBCrypt(VOID)
{
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
    static const UCHAR AesCmacKey[] =
    {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const UCHAR HmacKey[] = {'k', 'e', 'y'};
    static const UCHAR QuickFox[] =
        "The quick brown fox jumps over the lazy dog";
    static const UCHAR Abc[] = {'a', 'b', 'c'};
    NTSTATUS Status;

    SymCryptInit();

    Status = KsecBcryptHashKnownAnswer(BCRYPT_SHA256_ALGORITHM,
                                       0,
                                       NULL,
                                       0,
                                       Abc,
                                       sizeof(Abc),
                                       Sha256Abc,
                                       sizeof(Sha256Abc));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = KsecBcryptHashKnownAnswer(BCRYPT_SHA256_ALGORITHM,
                                       BCRYPT_ALG_HANDLE_HMAC_FLAG,
                                       HmacKey,
                                       sizeof(HmacKey),
                                       QuickFox,
                                       sizeof(QuickFox) - 1,
                                       HmacSha256QuickFox,
                                       sizeof(HmacSha256QuickFox));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = KsecBcryptHashKnownAnswer(KsecAesCmacAlgorithm,
                                       0,
                                       AesCmacKey,
                                       sizeof(AesCmacKey),
                                       NULL,
                                       0,
                                       AesCmacEmpty,
                                       sizeof(AesCmacEmpty));
    if (!NT_SUCCESS(Status))
        return Status;

    DPRINT1("KSECDD: kernel BCrypt provider initialized; SHA-256, HMAC-SHA-256 and AES-CMAC KATs passed\n");
    return STATUS_SUCCESS;
}
