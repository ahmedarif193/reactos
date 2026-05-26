/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Minimal soft-float helpers to keep kernel logging working
 *                  until the real floating-point runtime support is wired up.
 */

#include <ntoskrnl.h>
#include <limits.h>
#include <fpstate.h>

/* TODO(ARM64): Replace these placeholders with proper libgcc/libm quality
 * implementations once the ARM64 ABI work is complete. */

/*
 * ARM64 per-thread FP state:
 * - Stored directly in KTHREAD.Arm64FpState.
 * - Lazy, trap-on-first-use semantics with per-CPU owner tracking.
 * - Full NEON and SVE register state save/restore.
 */
#define KI_ARM64_FP_STATE_TAG        'pF6A'
#define KI_ARM64_FP_FLAG_NEON_VALID  0x00000001UL
#define KI_ARM64_FP_FLAG_SVE_USED    0x00000002UL
#define KI_ARM64_FP_FLAG_SVE_VALID   0x00000004UL

typedef struct _KI_ARM64_SVE_STATE
{
    ULONG Size;
    ULONG VectorLengthBytes;
    ULONG VectorLengthBits;
    ULONG PredBytes;
    ULONG64 Zcr;
    ULONG Fpcr;
    ULONG Fpsr;
    ULONG Reserved;
    UCHAR Data[ANYSIZE_ARRAY];
} KI_ARM64_SVE_STATE, *PKI_ARM64_SVE_STATE;

typedef struct _KI_ARM64_THREAD_FP_STATE
{
    ULONG Flags;
    ULONG Reserved;
    DECLSPEC_ALIGN(16) KARM64_NEON_STATE NeonState;
    PKI_ARM64_SVE_STATE SveState;
#if DBG
    ULONG DebugExpectedTrap;
    ULONG DebugTrapCount;
    ULONG DebugSaveCount;
    ULONG DebugRestoreCount;
    ULONG DebugLastCpu;
    ULONG DebugLastEsrClass;
    ULONG64 DebugNeonChecksum;
    ULONG64 DebugSveChecksum;
#endif
} KI_ARM64_THREAD_FP_STATE, *PKI_ARM64_THREAD_FP_STATE;

static volatile LONG KiArm64FpStateInitialized = 0;
static KSPIN_LOCK KiArm64FpOwnerLock;
static PKTHREAD KiArm64FpOwnerThread[MAXIMUM_PROCESSORS];

BOOLEAN KiArm64HasNeon = TRUE;
BOOLEAN KiArm64HasSve = FALSE;
BOOLEAN KiArm64HasSme = FALSE;
ULONG KiArm64MaxSveVectorLength = 0;
ULONG KiArm64SveStateSize = KI_NEON_STATE_SIZE;

#define KI_ARM64_SVE_SAVE_ZREG(_Reg, _Ptr) \
    __asm__ __volatile__(".arch_extension sve\n\tstr z" #_Reg ", [%0]\n\taddvl %0, %0, #1\n" \
                         : "+r"(_Ptr) : : "memory")

#define KI_ARM64_SVE_RESTORE_ZREG(_Reg, _Ptr) \
    __asm__ __volatile__(".arch_extension sve\n\tldr z" #_Reg ", [%0]\n\taddvl %0, %0, #1\n" \
                         : "+r"(_Ptr) : : "memory")

#define KI_ARM64_SVE_SAVE_PREG(_Reg, _Ptr) \
    __asm__ __volatile__(".arch_extension sve\n\tstr p" #_Reg ", [%0]\n" \
                         : : "r"(_Ptr) : "memory")

#define KI_ARM64_SVE_RESTORE_PREG(_Reg, _Ptr) \
    __asm__ __volatile__(".arch_extension sve\n\tldr p" #_Reg ", [%0]\n" \
                         : : "r"(_Ptr) : "memory")

static
ULONG
KiArm64ComputeSveStateSize(
    _In_ ULONG VectorLengthBytes);

static
ULONG
KiArm64SvePayloadSize(
    _In_ PKI_ARM64_SVE_STATE Sve);

#if DBG
static
ULONG64
KiArm64DbgChecksumBytes(
    _In_reads_bytes_(Size) const UCHAR *Buffer,
    _In_ SIZE_T Size)
{
    ULONG64 Hash;
    SIZE_T Index;

    Hash = 0xA0761D6478BD642FULL;
    for (Index = 0; Index < Size; Index++)
    {
        Hash ^= (ULONG64)Buffer[Index] + 0x9E3779B185EBCA87ULL + (Hash << 7) + (Hash >> 3);
    }

    return Hash;
}

static
ULONG64
KiArm64DbgChecksumNeonState(
    _In_ const KARM64_NEON_STATE *Neon)
{
    return KiArm64DbgChecksumBytes((const UCHAR *)Neon, sizeof(*Neon));
}

static
VOID
KiArm64DbgAssertSveLayout(
    _In_ const KI_ARM64_SVE_STATE *Sve)
{
    ULONG ExpectedSize;

    ASSERT(Sve != NULL);
    ASSERT(Sve->VectorLengthBytes != 0);
    ASSERT((Sve->VectorLengthBits / 8) == Sve->VectorLengthBytes);
    ASSERT(Sve->PredBytes == (Sve->VectorLengthBytes / 8));

    ExpectedSize = KiArm64ComputeSveStateSize(Sve->VectorLengthBytes);
    ASSERT(Sve->Size >= ExpectedSize);
    ASSERT((Sve->VectorLengthBits >= KI_SVE_MIN_VL) &&
           (Sve->VectorLengthBits <= KI_SVE_MAX_VL) &&
           ((Sve->VectorLengthBits & (KI_SVE_MIN_VL - 1)) == 0));
}

static
ULONG64
KiArm64DbgChecksumSveState(
    _In_ const KI_ARM64_SVE_STATE *Sve)
{
    ULONG64 Hash;

    KiArm64DbgAssertSveLayout(Sve);
    Hash = Sve->Zcr ^ ((ULONG64)Sve->Fpcr << 32) ^ (ULONG64)Sve->Fpsr;
    Hash ^= KiArm64DbgChecksumBytes(Sve->Data, KiArm64SvePayloadSize((PKI_ARM64_SVE_STATE)Sve));
    return Hash;
}

static
VOID
KiArm64DbgAssertOwnerUniqueLocked(
    _In_ ULONG CpuNumber,
    _In_ PKTHREAD Thread)
{
    ULONG Index;

    ASSERT(CpuNumber < MAXIMUM_PROCESSORS);
    ASSERT(Thread != NULL);

    for (Index = 0; Index < MAXIMUM_PROCESSORS; Index++)
    {
        if (Index != CpuNumber)
        {
            ASSERT(KiArm64FpOwnerThread[Index] != Thread);
        }
    }
}

static
VOID
KiArm64DbgMarkSavedState(
    _Inout_ PKI_ARM64_THREAD_FP_STATE State,
    _In_ BOOLEAN SavedSve,
    _In_ ULONG CpuNumber)
{
    ASSERT(State != NULL);
    ASSERT(CpuNumber < MAXIMUM_PROCESSORS);

    State->DebugExpectedTrap = 1;
    State->DebugSaveCount++;
    State->DebugLastCpu = CpuNumber;

    if (SavedSve)
    {
        ASSERT(State->SveState != NULL);
        State->DebugSveChecksum = KiArm64DbgChecksumSveState(State->SveState);
    }
    else
    {
        State->DebugNeonChecksum = KiArm64DbgChecksumNeonState(&State->NeonState);
    }
}

static
VOID
KiArm64DbgAssertRestoreInput(
    _Inout_ PKI_ARM64_THREAD_FP_STATE State,
    _In_ BOOLEAN RestoreSve,
    _In_ ULONG CpuNumber)
{
    ASSERT(State != NULL);
    ASSERT(CpuNumber < MAXIMUM_PROCESSORS);

    if (RestoreSve)
    {
        ASSERT((State->Flags & KI_ARM64_FP_FLAG_SVE_VALID) != 0);
        ASSERT(State->SveState != NULL);
        if (State->DebugSaveCount != 0)
        {
            ASSERT(State->DebugSveChecksum == KiArm64DbgChecksumSveState(State->SveState));
        }
    }
    else
    {
        ASSERT((State->Flags & KI_ARM64_FP_FLAG_NEON_VALID) != 0);
        if (State->DebugSaveCount != 0)
        {
            ASSERT(State->DebugNeonChecksum == KiArm64DbgChecksumNeonState(&State->NeonState));
        }
    }

    State->DebugRestoreCount++;
    State->DebugLastCpu = CpuNumber;
}

static
VOID
KiArm64DbgMarkTrapHandled(
    _Inout_ PKI_ARM64_THREAD_FP_STATE State,
    _In_ ULONG EsrClass,
    _In_ BOOLEAN RequireExpectedTrap,
    _In_ ULONG CpuNumber)
{
    ASSERT(State != NULL);
    ASSERT(CpuNumber < MAXIMUM_PROCESSORS);

    /*
     * If the thread had saved FP state, this trap must come from a re-armed
     * trap-on-first-use switch-in path.
     */
    if (RequireExpectedTrap)
    {
        ASSERT(State->DebugExpectedTrap != 0);
    }

    State->DebugExpectedTrap = 0;
    State->DebugTrapCount++;
    State->DebugLastCpu = CpuNumber;
    State->DebugLastEsrClass = EsrClass;
}
#endif

static
VOID
KiArm64EnsureFpStateInitialized(
    VOID)
{
    if (InterlockedCompareExchange(&KiArm64FpStateInitialized, 1, 0) == 0)
    {
        KeInitializeSpinLock(&KiArm64FpOwnerLock);
        RtlZeroMemory(KiArm64FpOwnerThread, sizeof(KiArm64FpOwnerThread));
    }
}

static
PKI_ARM64_THREAD_FP_STATE
KiArm64GetThreadFpState(
    _Inout_ PKTHREAD Thread,
    _In_ BOOLEAN CreateIfMissing)
{
    PKI_ARM64_THREAD_FP_STATE State;
    PKI_ARM64_THREAD_FP_STATE NewState;
    PVOID Previous;

    State = (PKI_ARM64_THREAD_FP_STATE)Thread->Arm64FpState;
    if ((State != NULL) || !CreateIfMissing)
    {
        return State;
    }

    NewState = ExAllocatePoolWithTag(NonPagedPool, sizeof(*NewState), KI_ARM64_FP_STATE_TAG);
    if (NewState == NULL)
    {
        return NULL;
    }

    RtlZeroMemory(NewState, sizeof(*NewState));
    Previous = InterlockedCompareExchangePointer((PVOID volatile *)&Thread->Arm64FpState,
                                                 NewState,
                                                 NULL);
    if (Previous != NULL)
    {
        ExFreePoolWithTag(NewState, KI_ARM64_FP_STATE_TAG);
        return (PKI_ARM64_THREAD_FP_STATE)Previous;
    }

    return NewState;
}

static
ULONG
KiArm64QuerySveVectorLengthBytes(
    VOID)
{
    ULONG64 VlBytes = 0;

    __asm__ __volatile__(".arch_extension sve\n\trdvl %0, #1\n" : "=r"(VlBytes));
    return (ULONG)VlBytes;
}

static
ULONG
KiArm64ComputeSveStateSize(
    _In_ ULONG VectorLengthBytes)
{
    ULONG PredBytes;
    ULONG Size;

    PredBytes = VectorLengthBytes / 8;
    Size = sizeof(KI_ARM64_SVE_STATE) +
           (32 * VectorLengthBytes) +      /* Z0-Z31 */
           (17 * PredBytes);               /* P0-P15 + FFR */

    return (Size + 15) & ~15;
}

static
ULONG
KiArm64SvePayloadSize(
    _In_ PKI_ARM64_SVE_STATE Sve)
{
    return (32 * Sve->VectorLengthBytes) + (17 * Sve->PredBytes);
}

static
BOOLEAN
KiArm64EnsureSveStateBuffer(
    _Inout_ PKI_ARM64_THREAD_FP_STATE State)
{
    ULONG VlBits;
    ULONG VlBytes;
    ULONG Size;
    PKI_ARM64_SVE_STATE Sve;

    if (State->SveState != NULL)
    {
#if DBG
        KiArm64DbgAssertSveLayout(State->SveState);
#endif
        return TRUE;
    }

    VlBits = KiArm64MaxSveVectorLength;
    if ((VlBits < KI_SVE_MIN_VL) ||
        (VlBits > KI_SVE_MAX_VL) ||
        ((VlBits & (KI_SVE_MIN_VL - 1)) != 0))
    {
        return FALSE;
    }

    VlBytes = VlBits / 8;
    Size = KiArm64ComputeSveStateSize(VlBytes);

    Sve = ExAllocatePoolWithTag(NonPagedPool, Size, KI_ARM64_FP_STATE_TAG);
    if (Sve == NULL)
    {
        return FALSE;
    }

    RtlZeroMemory(Sve, Size);
    Sve->Size = Size;
    Sve->VectorLengthBytes = VlBytes;
    Sve->VectorLengthBits = VlBits;
    Sve->PredBytes = VlBytes / 8;
    State->SveState = Sve;
#if DBG
    KiArm64DbgAssertSveLayout(State->SveState);
#endif
    return TRUE;
}

static
VOID
KiArm64SeedSveFromNeon(
    _Inout_ PKI_ARM64_THREAD_FP_STATE State)
{
    ULONG Index;
    UCHAR *ZBase;
    PKI_ARM64_SVE_STATE Sve;

    Sve = State->SveState;
    if (Sve == NULL)
    {
        return;
    }

    RtlZeroMemory(Sve->Data, KiArm64SvePayloadSize(Sve));
    ZBase = Sve->Data;

    for (Index = 0; Index < 32; Index++)
    {
        RtlCopyMemory(ZBase + (Index * Sve->VectorLengthBytes),
                      State->NeonState.Q[Index],
                      16);
    }

    Sve->Fpcr = State->NeonState.Fpcr;
    Sve->Fpsr = State->NeonState.Fpsr;
    Sve->Zcr = (((ULONG64)(Sve->VectorLengthBytes / 16)) - 1ULL) & 0xFULL;
}

static
VOID
KiArm64SaveNeonState(
    _Out_ PKARM64_NEON_STATE State)
{
    ULONG64 FpcrTemp;
    ULONG64 FpsrTemp;

    __asm__ __volatile__("mrs %0, fpcr" : "=r"(FpcrTemp));
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(FpsrTemp));
    State->Fpcr = (ULONG)FpcrTemp;
    State->Fpsr = (ULONG)FpsrTemp;

    __asm__ __volatile__(
        "stp q0,  q1,  [%0, #(0 * 32)]      \n"
        "stp q2,  q3,  [%0, #(1 * 32)]      \n"
        "stp q4,  q5,  [%0, #(2 * 32)]      \n"
        "stp q6,  q7,  [%0, #(3 * 32)]      \n"
        "stp q8,  q9,  [%0, #(4 * 32)]      \n"
        "stp q10, q11, [%0, #(5 * 32)]      \n"
        "stp q12, q13, [%0, #(6 * 32)]      \n"
        "stp q14, q15, [%0, #(7 * 32)]      \n"
        "stp q16, q17, [%0, #(8 * 32)]      \n"
        "stp q18, q19, [%0, #(9 * 32)]      \n"
        "stp q20, q21, [%0, #(10 * 32)]     \n"
        "stp q22, q23, [%0, #(11 * 32)]     \n"
        "stp q24, q25, [%0, #(12 * 32)]     \n"
        "stp q26, q27, [%0, #(13 * 32)]     \n"
        "stp q28, q29, [%0, #(14 * 32)]     \n"
        "stp q30, q31, [%0, #(15 * 32)]     \n"
        :
        : "r"(State->Q)
        : "memory");
}

static
VOID
KiArm64RestoreNeonState(
    _In_ PKARM64_NEON_STATE State)
{
    __asm__ __volatile__(
        "ldp q0,  q1,  [%0, #(0 * 32)]      \n"
        "ldp q2,  q3,  [%0, #(1 * 32)]      \n"
        "ldp q4,  q5,  [%0, #(2 * 32)]      \n"
        "ldp q6,  q7,  [%0, #(3 * 32)]      \n"
        "ldp q8,  q9,  [%0, #(4 * 32)]      \n"
        "ldp q10, q11, [%0, #(5 * 32)]      \n"
        "ldp q12, q13, [%0, #(6 * 32)]      \n"
        "ldp q14, q15, [%0, #(7 * 32)]      \n"
        "ldp q16, q17, [%0, #(8 * 32)]      \n"
        "ldp q18, q19, [%0, #(9 * 32)]      \n"
        "ldp q20, q21, [%0, #(10 * 32)]     \n"
        "ldp q22, q23, [%0, #(11 * 32)]     \n"
        "ldp q24, q25, [%0, #(12 * 32)]     \n"
        "ldp q26, q27, [%0, #(13 * 32)]     \n"
        "ldp q28, q29, [%0, #(14 * 32)]     \n"
        "ldp q30, q31, [%0, #(15 * 32)]     \n"
        :
        : "r"(State->Q)
        : "memory");

    __asm__ __volatile__("msr fpcr, %0" :: "r"((ULONG64)State->Fpcr));
    __asm__ __volatile__("msr fpsr, %0" :: "r"((ULONG64)State->Fpsr));
}

static
VOID
KiArm64SaveSveState(
    _Inout_ PKI_ARM64_SVE_STATE Sve)
{
    UCHAR *ZPtr;
    UCHAR *PBase;
    UCHAR *FfrPtr;
    ULONG64 Zcr;
    ULONG64 FpcrTemp;
    ULONG64 FpsrTemp;

    __asm__ __volatile__("mrs %0, S3_0_C1_C2_0" : "=r"(Zcr));
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(FpcrTemp));
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(FpsrTemp));
    Sve->Zcr = Zcr;
    Sve->Fpcr = (ULONG)FpcrTemp;
    Sve->Fpsr = (ULONG)FpsrTemp;

    ZPtr = Sve->Data;
    KI_ARM64_SVE_SAVE_ZREG(0, ZPtr);   KI_ARM64_SVE_SAVE_ZREG(1, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(2, ZPtr);   KI_ARM64_SVE_SAVE_ZREG(3, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(4, ZPtr);   KI_ARM64_SVE_SAVE_ZREG(5, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(6, ZPtr);   KI_ARM64_SVE_SAVE_ZREG(7, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(8, ZPtr);   KI_ARM64_SVE_SAVE_ZREG(9, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(10, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(11, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(12, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(13, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(14, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(15, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(16, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(17, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(18, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(19, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(20, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(21, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(22, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(23, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(24, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(25, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(26, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(27, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(28, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(29, ZPtr);
    KI_ARM64_SVE_SAVE_ZREG(30, ZPtr);  KI_ARM64_SVE_SAVE_ZREG(31, ZPtr);

    PBase = Sve->Data + (32 * Sve->VectorLengthBytes);
    KI_ARM64_SVE_SAVE_PREG(0,  PBase + (0 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(1,  PBase + (1 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(2,  PBase + (2 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(3,  PBase + (3 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(4,  PBase + (4 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(5,  PBase + (5 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(6,  PBase + (6 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(7,  PBase + (7 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(8,  PBase + (8 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(9,  PBase + (9 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(10, PBase + (10 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(11, PBase + (11 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(12, PBase + (12 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(13, PBase + (13 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(14, PBase + (14 * Sve->PredBytes));
    KI_ARM64_SVE_SAVE_PREG(15, PBase + (15 * Sve->PredBytes));

    FfrPtr = PBase + (16 * Sve->PredBytes);
    __asm__ __volatile__(".arch_extension sve\n\trdffr p15.b\n\tstr p15, [%0]\n"
                         : : "r"(FfrPtr) : "memory");
}

static
VOID
KiArm64RestoreSveState(
    _In_ PKI_ARM64_SVE_STATE Sve)
{
    UCHAR *ZPtr;
    UCHAR *PBase;
    UCHAR *FfrPtr;

    __asm__ __volatile__("msr S3_0_C1_C2_0, %0" :: "r"(Sve->Zcr));
    __asm__ __volatile__("isb" ::: "memory");

    ZPtr = Sve->Data;
    KI_ARM64_SVE_RESTORE_ZREG(0, ZPtr);   KI_ARM64_SVE_RESTORE_ZREG(1, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(2, ZPtr);   KI_ARM64_SVE_RESTORE_ZREG(3, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(4, ZPtr);   KI_ARM64_SVE_RESTORE_ZREG(5, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(6, ZPtr);   KI_ARM64_SVE_RESTORE_ZREG(7, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(8, ZPtr);   KI_ARM64_SVE_RESTORE_ZREG(9, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(10, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(11, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(12, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(13, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(14, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(15, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(16, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(17, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(18, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(19, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(20, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(21, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(22, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(23, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(24, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(25, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(26, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(27, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(28, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(29, ZPtr);
    KI_ARM64_SVE_RESTORE_ZREG(30, ZPtr);  KI_ARM64_SVE_RESTORE_ZREG(31, ZPtr);

    PBase = Sve->Data + (32 * Sve->VectorLengthBytes);
    KI_ARM64_SVE_RESTORE_PREG(0,  PBase + (0 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(1,  PBase + (1 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(2,  PBase + (2 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(3,  PBase + (3 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(4,  PBase + (4 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(5,  PBase + (5 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(6,  PBase + (6 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(7,  PBase + (7 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(8,  PBase + (8 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(9,  PBase + (9 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(10, PBase + (10 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(11, PBase + (11 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(12, PBase + (12 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(13, PBase + (13 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(14, PBase + (14 * Sve->PredBytes));
    KI_ARM64_SVE_RESTORE_PREG(15, PBase + (15 * Sve->PredBytes));

    FfrPtr = PBase + (16 * Sve->PredBytes);
    __asm__ __volatile__(".arch_extension sve\n\tldr p15, [%0]\n\twrffr p15.b\n"
                         : : "r"(FfrPtr) : "memory");

    __asm__ __volatile__("msr fpcr, %0" :: "r"((ULONG64)Sve->Fpcr));
    __asm__ __volatile__("msr fpsr, %0" :: "r"((ULONG64)Sve->Fpsr));
}

static
VOID
KiArm64SaveOwnerStateUnsafe(
    _In_ ULONG CpuNumber)
{
    PKTHREAD Owner;
    PKI_ARM64_THREAD_FP_STATE State;
    BOOLEAN SavedSve;

    ASSERT(CpuNumber < MAXIMUM_PROCESSORS);

    Owner = KiArm64FpOwnerThread[CpuNumber];
    if (Owner == NULL)
    {
        return;
    }

    State = KiArm64GetThreadFpState(Owner, FALSE);
    SavedSve = FALSE;
    if (State != NULL)
    {
        if ((State->Flags & KI_ARM64_FP_FLAG_SVE_USED) &&
            (State->SveState != NULL))
        {
            KiArm64SaveSveState(State->SveState);
            State->Flags |= KI_ARM64_FP_FLAG_SVE_VALID | KI_ARM64_FP_FLAG_NEON_VALID;
            SavedSve = TRUE;
        }
        else
        {
            KiArm64SaveNeonState(&State->NeonState);
            State->Flags |= KI_ARM64_FP_FLAG_NEON_VALID;
        }
#if DBG
        KiArm64DbgMarkSavedState(State, SavedSve, CpuNumber);
#endif
    }

    KiArm64FpOwnerThread[CpuNumber] = NULL;
}

ULONG
KiArm64GetSveStateSize(
    _In_ ULONG VectorLengthBits)
{
    ULONG VlBytes;
    ULONG Size;

    if ((VectorLengthBits < KI_SVE_MIN_VL) ||
        (VectorLengthBits > KI_SVE_MAX_VL) ||
        ((VectorLengthBits & (KI_SVE_MIN_VL - 1)) != 0))
    {
        return KI_NEON_STATE_SIZE;
    }

    VlBytes = VectorLengthBits / 8;
    Size = (32 * VlBytes) +             /* Z registers */
           (2 * VlBytes) +              /* P registers */
           (VlBytes / 8) +              /* FFR */
           16;                          /* Control + alignment */

    return (Size + 15) & ~15;
}

VOID
KiArm64InitializeFpSupport(
    VOID)
{
    ULONG64 Pfr0;
    ULONG64 Pfr1;
    ULONG64 Cpacr;
    ULONG VlBytes;

    KiArm64EnsureFpStateInitialized();

    __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(Pfr0));
    __asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(Pfr1));

    KiArm64HasNeon = (((Pfr0 >> 20) & 0xF) != 0xF);
    KiArm64HasSve = (((Pfr0 >> 32) & 0xF) != 0);

    /*
     * SME support needs full ZA state save/restore.
     * Keep it disabled until dedicated SME context code is in place.
     */
    KiArm64HasSme = FALSE;
    if (((Pfr1 >> 24) & 0xF) != 0)
    {
        /* Hardware may support SME, but kernel support is intentionally disabled. */
    }

    if (KiArm64HasSve)
    {
        __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
        Cpacr |= CPACR_EL1_FPEN_ENABLE | CPACR_EL1_ZEN_ENABLE;
        __asm__ __volatile__("msr cpacr_el1, %0" :: "r"(Cpacr));
        __asm__ __volatile__("isb" ::: "memory");

        VlBytes = KiArm64QuerySveVectorLengthBytes();
        if ((VlBytes < (KI_SVE_MIN_VL / 8)) ||
            (VlBytes > (KI_SVE_MAX_VL / 8)) ||
            ((VlBytes % (KI_SVE_MIN_VL / 8)) != 0))
        {
            VlBytes = KI_SVE_MIN_VL / 8;
        }

        KiArm64MaxSveVectorLength = VlBytes * 8;
        KiArm64SveStateSize = KiArm64GetSveStateSize(KiArm64MaxSveVectorLength);
    }
    else
    {
        KiArm64MaxSveVectorLength = 0;
        KiArm64SveStateSize = KI_NEON_STATE_SIZE;
    }
}

BOOLEAN
KiArm64HandleFpTrap(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Esr)
{
    ULONG EsrClass;
    KIRQL Irql;
    PKTHREAD Thread;
    PKI_ARM64_THREAD_FP_STATE State;
    KIRQL OldIrql;
    ULONG CpuNumber;
    BOOLEAN IsSveTrap;
#if DBG
    BOOLEAN HadSavedState;
    BOOLEAN RequireExpectedTrap;
#endif

    UNREFERENCED_PARAMETER(TrapFrame);

#if DBG
    {
        ULONG64 Cpacr;
        __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
        ASSERT((Cpacr & CPACR_EL1_FPEN_MASK) != CPACR_EL1_FPEN_TRAP);
    }
#endif

    EsrClass = (Esr >> 26) & 0x3F;
    IsSveTrap = (EsrClass == ESR_EC_SVE_TRAP);

    if (EsrClass == ESR_EC_SME_TRAP)
    {
        return FALSE;
    }

    if (!IsSveTrap && (EsrClass != ESR_EC_FP_TRAP))
    {
        return FALSE;
    }

    if (IsSveTrap ? !KiArm64HasSve : !KiArm64HasNeon)
    {
        return FALSE;
    }

    Irql = KeGetCurrentIrql();
    if (Irql > DISPATCH_LEVEL)
    {
#if DBG
        PKTHREAD OwnerBefore;
        ULONG FastCpu;
        ULONG64 Cpacr;

        FastCpu = KeGetCurrentProcessorNumber();
        OwnerBefore = (FastCpu < MAXIMUM_PROCESSORS) ? KiArm64FpOwnerThread[FastCpu] : NULL;
#endif
        /*
         * High-IRQL trap handling must not allocate or take bookkeeping locks.
         * Enable execution and defer ownership/state tracking to a safe IRQL.
         */
        if (IsSveTrap)
        {
            KiArm64EnableSve();
        }
        else
        {
            KiArm64EnableFp();
        }
#if DBG
        __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
        ASSERT((Cpacr & CPACR_EL1_FPEN_MASK) == CPACR_EL1_FPEN_ENABLE);
        if (IsSveTrap)
        {
            ASSERT((Cpacr & CPACR_EL1_ZEN_MASK) == CPACR_EL1_ZEN_ENABLE);
        }
        if (FastCpu < MAXIMUM_PROCESSORS)
        {
            ASSERT(KiArm64FpOwnerThread[FastCpu] == OwnerBefore);
        }
#endif
        return TRUE;
    }

    ASSERT(Irql <= DISPATCH_LEVEL);

    Thread = KeGetCurrentThread();
    if (Thread == NULL)
    {
        if (IsSveTrap)
        {
            KiArm64EnableSve();
        }
        else
        {
            KiArm64EnableFp();
        }
        return TRUE;
    }

    CpuNumber = KeGetCurrentProcessorNumber();
    if (CpuNumber >= MAXIMUM_PROCESSORS)
    {
        if (IsSveTrap)
        {
            KiArm64EnableSve();
        }
        else
        {
            KiArm64EnableFp();
        }
        return TRUE;
    }

    State = KiArm64GetThreadFpState(Thread, TRUE);
    if (State == NULL)
    {
        return FALSE;
    }

    if (IsSveTrap && !KiArm64EnsureSveStateBuffer(State))
    {
        return FALSE;
    }

    KeAcquireSpinLock(&KiArm64FpOwnerLock, &OldIrql);

    if (KiArm64HasSve)
    {
        KiArm64EnableSve();
    }
    else
    {
        KiArm64EnableFp();
    }

#if DBG
    RequireExpectedTrap = ((State->Flags & (KI_ARM64_FP_FLAG_NEON_VALID | KI_ARM64_FP_FLAG_SVE_VALID)) != 0) &&
                          (KiArm64FpOwnerThread[CpuNumber] != Thread);
#endif

    if (KiArm64FpOwnerThread[CpuNumber] != Thread)
    {
        KiArm64SaveOwnerStateUnsafe(CpuNumber);
    }

#if DBG
    HadSavedState = ((State->Flags & (KI_ARM64_FP_FLAG_NEON_VALID | KI_ARM64_FP_FLAG_SVE_VALID)) != 0);
    if (!HadSavedState)
    {
        RequireExpectedTrap = FALSE;
    }
    KiArm64DbgMarkTrapHandled(State, EsrClass, RequireExpectedTrap, CpuNumber);
#endif

    if (IsSveTrap)
    {
        if ((State->Flags & KI_ARM64_FP_FLAG_SVE_VALID) &&
            (State->SveState != NULL))
        {
#if DBG
            KiArm64DbgAssertRestoreInput(State, TRUE, CpuNumber);
#endif
            KiArm64RestoreSveState(State->SveState);
        }
        else
        {
            if (State->Flags & KI_ARM64_FP_FLAG_NEON_VALID)
            {
#if DBG
                KiArm64DbgAssertRestoreInput(State, FALSE, CpuNumber);
#endif
                KiArm64SeedSveFromNeon(State);
            }
            KiArm64RestoreSveState(State->SveState);
            State->Flags |= KI_ARM64_FP_FLAG_SVE_VALID;
        }

        State->Flags |= KI_ARM64_FP_FLAG_SVE_USED |
                        KI_ARM64_FP_FLAG_NEON_VALID;
    }
    else
    {
        if ((State->Flags & KI_ARM64_FP_FLAG_SVE_USED) &&
            (State->Flags & KI_ARM64_FP_FLAG_SVE_VALID) &&
            (State->SveState != NULL))
        {
            KiArm64EnableSve();
#if DBG
            KiArm64DbgAssertRestoreInput(State, TRUE, CpuNumber);
#endif
            KiArm64RestoreSveState(State->SveState);
        }
        else
        {
            KiArm64EnableFp();
#if DBG
            if ((State->Flags & KI_ARM64_FP_FLAG_NEON_VALID) != 0)
            {
                KiArm64DbgAssertRestoreInput(State, FALSE, CpuNumber);
            }
#endif
            KiArm64RestoreNeonState(&State->NeonState);
            State->Flags |= KI_ARM64_FP_FLAG_NEON_VALID;
        }
    }

    KiArm64FpOwnerThread[CpuNumber] = Thread;
#if DBG
    KiArm64DbgAssertOwnerUniqueLocked(CpuNumber, Thread);
#endif
    KeReleaseSpinLock(&KiArm64FpOwnerLock, OldIrql);
    return TRUE;
}

VOID
KiArm64SaveFpState(
    _Inout_ PKTHREAD Thread)
{
    KIRQL OldIrql;
    ULONG CpuNumber;
    PKI_ARM64_THREAD_FP_STATE State;

    if ((Thread == NULL) ||
        (InterlockedCompareExchange(&KiArm64FpStateInitialized, 1, 1) == 0))
    {
        return;
    }

    CpuNumber = KeGetCurrentProcessorNumber();
    if (CpuNumber >= MAXIMUM_PROCESSORS)
    {
        return;
    }

    KeAcquireSpinLock(&KiArm64FpOwnerLock, &OldIrql);

    if (KiArm64FpOwnerThread[CpuNumber] == Thread)
    {
        State = KiArm64GetThreadFpState(Thread, FALSE);
        if (State != NULL)
        {
            if ((State->Flags & KI_ARM64_FP_FLAG_SVE_USED) &&
                (State->SveState != NULL))
            {
                KiArm64EnableSve();
                KiArm64SaveSveState(State->SveState);
                State->Flags |= KI_ARM64_FP_FLAG_SVE_VALID | KI_ARM64_FP_FLAG_NEON_VALID;
#if DBG
                KiArm64DbgMarkSavedState(State, TRUE, CpuNumber);
#endif
            }
            else
            {
                KiArm64EnableFp();
                KiArm64SaveNeonState(&State->NeonState);
                State->Flags |= KI_ARM64_FP_FLAG_NEON_VALID;
#if DBG
                KiArm64DbgMarkSavedState(State, FALSE, CpuNumber);
#endif
            }
        }

        KiArm64FpOwnerThread[CpuNumber] = NULL;
    }

    KeReleaseSpinLock(&KiArm64FpOwnerLock, OldIrql);
}

VOID
KiArm64RestoreFpState(
    _Inout_ PKTHREAD Thread)
{
    KIRQL OldIrql;
    ULONG CpuNumber;
    PKI_ARM64_THREAD_FP_STATE State;

    if (Thread == NULL)
    {
        KiArm64ConfigureFpTrap();
#if DBG
        {
            ULONG64 Cpacr;
            __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
            ASSERT((Cpacr & CPACR_EL1_FPEN_MASK) == CPACR_EL1_FPEN_EL0_TRAP);
        }
#endif
        return;
    }

    State = KiArm64GetThreadFpState(Thread, FALSE);
    if (State == NULL)
    {
        KiArm64ConfigureFpTrap();
#if DBG
        {
            ULONG64 Cpacr;
            __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
            ASSERT((Cpacr & CPACR_EL1_FPEN_MASK) == CPACR_EL1_FPEN_EL0_TRAP);
        }
#endif
        return;
    }

    CpuNumber = KeGetCurrentProcessorNumber();
    if (CpuNumber >= MAXIMUM_PROCESSORS)
    {
        KiArm64ConfigureFpTrap();
#if DBG
        {
            ULONG64 Cpacr;
            __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
            ASSERT((Cpacr & CPACR_EL1_FPEN_MASK) == CPACR_EL1_FPEN_EL0_TRAP);
        }
#endif
        return;
    }

    KeAcquireSpinLock(&KiArm64FpOwnerLock, &OldIrql);

    if ((State->Flags & KI_ARM64_FP_FLAG_SVE_USED) &&
        (State->Flags & KI_ARM64_FP_FLAG_SVE_VALID) &&
        (State->SveState != NULL))
    {
        KiArm64EnableSve();
        if (KiArm64FpOwnerThread[CpuNumber] != Thread)
        {
            KiArm64SaveOwnerStateUnsafe(CpuNumber);
        }
#if DBG
        KiArm64DbgAssertRestoreInput(State, TRUE, CpuNumber);
        State->DebugExpectedTrap = 0;
#endif
        KiArm64RestoreSveState(State->SveState);
        KiArm64FpOwnerThread[CpuNumber] = Thread;
    }
    else if (State->Flags & KI_ARM64_FP_FLAG_NEON_VALID)
    {
        KiArm64EnableFp();
        if (KiArm64FpOwnerThread[CpuNumber] != Thread)
        {
            KiArm64SaveOwnerStateUnsafe(CpuNumber);
        }
#if DBG
        KiArm64DbgAssertRestoreInput(State, FALSE, CpuNumber);
        State->DebugExpectedTrap = 0;
#endif
        KiArm64RestoreNeonState(&State->NeonState);
        KiArm64FpOwnerThread[CpuNumber] = Thread;
    }
    else
    {
        KeReleaseSpinLock(&KiArm64FpOwnerLock, OldIrql);
        KiArm64ConfigureFpTrap();
#if DBG
        {
            ULONG64 Cpacr;
            __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
            ASSERT((Cpacr & CPACR_EL1_FPEN_MASK) == CPACR_EL1_FPEN_EL0_TRAP);
        }
#endif
        return;
    }

#if DBG
    KiArm64DbgAssertOwnerUniqueLocked(CpuNumber, Thread);
#endif
    KeReleaseSpinLock(&KiArm64FpOwnerLock, OldIrql);
}

VOID
KiArm64FreeFpState(
    _Inout_ PKTHREAD Thread)
{
    KIRQL OldIrql;
    ULONG CpuNumber;
    PKI_ARM64_THREAD_FP_STATE State;

    if (Thread == NULL)
    {
        return;
    }

    State = (PKI_ARM64_THREAD_FP_STATE)Thread->Arm64FpState;
    Thread->Arm64FpState = NULL;
    if (State == NULL)
    {
        return;
    }

    KeAcquireSpinLock(&KiArm64FpOwnerLock, &OldIrql);
    for (CpuNumber = 0; CpuNumber < MAXIMUM_PROCESSORS; CpuNumber++)
    {
        if (KiArm64FpOwnerThread[CpuNumber] == Thread)
        {
            KiArm64FpOwnerThread[CpuNumber] = NULL;
        }
    }
    KeReleaseSpinLock(&KiArm64FpOwnerLock, OldIrql);

    if (State->SveState != NULL)
    {
        ExFreePoolWithTag(State->SveState, KI_ARM64_FP_STATE_TAG);
    }
    ExFreePoolWithTag(State, KI_ARM64_FP_STATE_TAG);
}

long double
__multf3(
    _In_ long double A,
    _In_ long double B)
{
    return A * B;
}

long double
__addtf3(
    _In_ long double A,
    _In_ long double B)
{
    return A + B;
}

long double
__divtf3(
    _In_ long double A,
    _In_ long double B)
{
    return (B == 0.0L) ? 0.0L : (A / B);
}

long double
__extenddftf2(
    _In_ double Value)
{
    return (long double)Value;
}

double
__trunctfdf2(
    _In_ long double Value)
{
    return (double)Value;
}

long double
__floatsitf(
    _In_ int Value)
{
    return (long double)Value;
}

long double
__floatunditf(
    _In_ unsigned long long Value)
{
    return (long double)Value;
}

unsigned long long
__fixunstfdi(
    _In_ long double Value)
{
    if (Value <= 0.0L)
    {
        return 0ULL;
    }

    if (Value >= (long double)ULLONG_MAX)
    {
        return ULLONG_MAX;
    }

    return (unsigned long long)Value;
}

int
__letf2(
    _In_ long double A,
    _In_ long double B)
{
    if (A < B)
    {
        return -1;
    }
    if (A > B)
    {
        return 1;
    }
    return 0;
}

int
__gttf2(
    _In_ long double A,
    _In_ long double B)
{
    if (A > B)
    {
        return 1;
    }
    if (A < B)
    {
        return -1;
    }
    return 0;
}

int
__lttf2(
    _In_ long double A,
    _In_ long double B)
{
    return (A < B) ? -1 : 0;
}

double
floor(
    _In_ double Value)
{
    LONG64 IntegerPart = (LONG64)Value;

    if ((double)IntegerPart > Value)
    {
        IntegerPart--;
    }

    return (double)IntegerPart;
}

double
pow(
    _In_ double Base,
    _In_ double Exponent)
{
    if (Exponent == 0.0)
    {
        return 1.0;
    }

    if (Base == 10.0)
    {
        double Result = 1.0;
        BOOLEAN Negative = FALSE;
        double Adjusted = Exponent;

        if (Adjusted < 0.0)
        {
            Negative = TRUE;
            Adjusted = -Adjusted;
        }

        LONG64 Whole = (LONG64)(Adjusted + 0.5);
        for (LONG64 Index = 0; Index < Whole; ++Index)
        {
            Result *= 10.0;
        }

        if (Negative && Result != 0.0)
        {
            Result = 1.0 / Result;
        }

        return Result;
    }

    /* Fallback stub */
    return 1.0;
}

double
log10(
    _In_ double Value)
{
    double Temp;
    double Result = 0.0;

    if (Value <= 0.0)
    {
        return 0.0;
    }

    Temp = Value;
    while (Temp >= 10.0)
    {
        Temp /= 10.0;
        Result += 1.0;
    }

    while (Temp > 0.0 && Temp < 1.0)
    {
        Temp *= 10.0;
        Result -= 1.0;
    }

    return Result;
}
