/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 extended-affinity ABI and core operation tests
 */

#include <kmt_test.h>

VOID Test_KeArm64AffinityEx(VOID);

#ifdef _M_ARM64
#define KMT_AFFINITY_EX2_GROUPS 64

typedef struct _KMT_AFFINITY_EX2
{
    USHORT Count;
    USHORT Size;
    ULONG Reserved;
    KAFFINITY Bitmap[KMT_AFFINITY_EX2_GROUPS];
} KMT_AFFINITY_EX2;

typedef struct _KMT_AFFINITY_EX2_BUFFER
{
    UCHAR GuardBefore[32];
    KMT_AFFINITY_EX2 Affinity;
    UCHAR GuardAfter[32];
} KMT_AFFINITY_EX2_BUFFER, *PKMT_AFFINITY_EX2_BUFFER;

typedef struct _KMT_AFFINITY_ENUMERATION_CONTEXT_BUFFER
{
    UCHAR GuardBefore[32];
    KAFFINITY_ENUMERATION_CONTEXT Context;
    UCHAR GuardAfter[32];
} KMT_AFFINITY_ENUMERATION_CONTEXT_BUFFER;

typedef struct _KMT_GROUP_AFFINITY_BUFFER
{
    UCHAR GuardBefore[32];
    GROUP_AFFINITY Affinity;
    UCHAR GuardAfter[32];
} KMT_GROUP_AFFINITY_BUFFER;

typedef struct _KMT_PROCESSOR_INDEX_BUFFER
{
    UCHAR GuardBefore[32];
    ULONG ProcessorIndex;
    UCHAR GuardAfter[32];
} KMT_PROCESSOR_INDEX_BUFFER;

C_ASSERT(FIELD_OFFSET(KMT_AFFINITY_EX2, Bitmap) == FIELD_OFFSET(KAFFINITY_EX, Bitmap));
C_ASSERT(sizeof(KAFFINITY_ENUMERATION_CONTEXT) == 24);
C_ASSERT(FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, Affinity) == 0);
C_ASSERT(FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentAffinity) == 8);
C_ASSERT(FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) == 16);
C_ASSERT(sizeof(GROUP_AFFINITY) == 16);
C_ASSERT(FIELD_OFFSET(GROUP_AFFINITY, Mask) == 0);
C_ASSERT(FIELD_OFFSET(GROUP_AFFINITY, Group) == 8);
C_ASSERT(FIELD_OFFSET(GROUP_AFFINITY, Reserved) == 10);
C_ASSERT(FIELD_OFFSET(KMT_PROCESSOR_INDEX_BUFFER, ProcessorIndex) == 32);

static ULONG
KmtCountSetBits(
    _In_ KAFFINITY Mask)
{
    ULONG Count = 0;

    while (Mask != 0)
    {
        Mask &= Mask - 1;
        Count++;
    }

    return Count;
}

typedef LOGICAL (NTAPI *PKMT_KE_AND_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Out_opt_ PKAFFINITY_EX Result);
typedef LOGICAL (NTAPI *PKMT_KE_AND_AFFINITY_EX2)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Inout_opt_ PKAFFINITY_EX Result);
typedef LOGICAL (NTAPI *PKMT_KE_AND_GROUP_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity, _In_ PGROUP_AFFINITY GroupAffinity, _Out_opt_ PGROUP_AFFINITY Result);
typedef VOID (NTAPI *PKMT_KE_ADD_PROCESSOR_GROUP_AFFINITY)(_Inout_ PGROUP_AFFINITY GroupAffinity, _In_ ULONG ProcessorIndex);
typedef LOGICAL (NTAPI *PKMT_KE_CHECK_PROCESSOR_GROUP_AFFINITY)(_In_ PGROUP_AFFINITY GroupAffinity, _In_ ULONG ProcessorIndex);
typedef VOID (NTAPI *PKMT_KE_COMPLEMENT_AFFINITY_EX)(_Out_ PKAFFINITY_EX Result, _In_ PKAFFINITY_EX Affinity);
typedef VOID (NTAPI *PKMT_KE_COMPLEMENT_AFFINITY_EX2)(_Inout_ PKAFFINITY_EX Result, _In_ PKAFFINITY_EX Affinity);
typedef VOID (NTAPI *PKMT_KE_COPY_AFFINITY_EX)(_Out_ PKAFFINITY_EX Destination, _In_ PKAFFINITY_EX Source);
typedef VOID (NTAPI *PKMT_KE_COPY_AFFINITY_EX2)(_Inout_ PKAFFINITY_EX Destination, _In_ PKAFFINITY_EX Source);
typedef ULONG (NTAPI *PKMT_KE_COUNT_SET_BITS_GROUP_AFFINITY)(_In_ PGROUP_AFFINITY GroupAffinity);
typedef NTSTATUS (NTAPI *PKMT_KE_ENUMERATE_NEXT_PROCESSOR)(_Out_ PULONG ProcessorIndex, _Inout_ PKAFFINITY_ENUMERATION_CONTEXT Context);
typedef ULONG (NTAPI *PKMT_KE_FIND_FIRST_SET_LEFT_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity);
typedef ULONG (NTAPI *PKMT_KE_FIND_FIRST_SET_LEFT_GROUP_AFFINITY)(_In_ PGROUP_AFFINITY GroupAffinity);
typedef ULONG (NTAPI *PKMT_KE_FIND_FIRST_SET_RIGHT_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity);
typedef ULONG (NTAPI *PKMT_KE_FIND_FIRST_SET_RIGHT_GROUP_AFFINITY)(_In_ PGROUP_AFFINITY GroupAffinity);
typedef NTSTATUS (NTAPI *PKMT_KE_FIRST_GROUP_AFFINITY_EX)(_Out_ PGROUP_AFFINITY GroupAffinity, _In_ PKAFFINITY_EX Affinity);
typedef VOID (NTAPI *PKMT_KE_INITIALIZE_AFFINITY_EX2)(_Out_ PKAFFINITY_EX Affinity, _In_ USHORT Size);
typedef VOID (NTAPI *PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT)(_Out_ PKAFFINITY_ENUMERATION_CONTEXT Context, _In_ PKAFFINITY_EX Affinity);
typedef VOID (NTAPI *PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT_FROM_AFFINITY)(_Out_ PKAFFINITY_ENUMERATION_CONTEXT Context, _In_ USHORT Group, _In_ KAFFINITY Affinity);
typedef VOID (NTAPI *PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT_FROM_GROUP)(_Out_ PKAFFINITY_ENUMERATION_CONTEXT Context, _In_ PGROUP_AFFINITY GroupAffinity);
typedef LOGICAL (NTAPI *PKMT_KE_INTERLOCKED_SET_PROCESSOR_AFFINITY_EX)(_Inout_ PKAFFINITY_EX Affinity, _In_ ULONG ProcessorIndex);
typedef SIZE_T (NTAPI *PKMT_KE_SIZE_OF_AFFINITY_EX)(_In_ USHORT Count);
typedef ULONG (NTAPI *PKMT_KE_GET_PROCESSOR_INDEX_FROM_NUMBER)(_In_ PPROCESSOR_NUMBER ProcessorNumber);
typedef NTSTATUS (NTAPI *PKMT_KE_GET_PROCESSOR_NUMBER_FROM_INDEX)(_In_ ULONG ProcessorIndex, _Out_ PPROCESSOR_NUMBER ProcessorNumber);
typedef ULONG (NTAPI *PKMT_KE_COUNT_SET_BITS_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity);
typedef LOGICAL (NTAPI *PKMT_KE_IS_EQUAL_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2);
typedef LOGICAL (NTAPI *PKMT_KE_IS_SINGLE_GROUP_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity, _Out_opt_ PUSHORT Group);
typedef LOGICAL (NTAPI *PKMT_KE_IS_SUBSET_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2);
typedef LOGICAL (NTAPI *PKMT_KE_OR_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Out_opt_ PKAFFINITY_EX Result);
typedef LOGICAL (NTAPI *PKMT_KE_OR_AFFINITY_EX2)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Inout_opt_ PKAFFINITY_EX Result);
typedef VOID (NTAPI *PKMT_KE_PROCESSOR_GROUP_AFFINITY)(_Out_ PGROUP_AFFINITY GroupAffinity, _In_ ULONG ProcessorIndex);
typedef VOID (NTAPI *PKMT_KE_REMOVE_PROCESSOR_GROUP_AFFINITY)(_Inout_ PGROUP_AFFINITY GroupAffinity, _In_ ULONG ProcessorIndex);
typedef LOGICAL (NTAPI *PKMT_KE_SUBTRACT_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Out_opt_ PKAFFINITY_EX Result);
typedef LOGICAL (NTAPI *PKMT_KE_SUBTRACT_AFFINITY_EX2)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Inout_opt_ PKAFFINITY_EX Result);

typedef struct _KMT_INTERLOCKED_SET_AFFINITY_CONTEXT
{
    PKMT_KE_INTERLOCKED_SET_PROCESSOR_AFFINITY_EX SetProcessorAffinityEx;
    PKAFFINITY_EX Affinity;
    volatile LONG TrueCount;
} KMT_INTERLOCKED_SET_AFFINITY_CONTEXT, *PKMT_INTERLOCKED_SET_AFFINITY_CONTEXT;

static VOID
KmtInitializeOrAffinityEx2Inputs(
    _Out_ PKMT_AFFINITY_EX2_BUFFER Buffer1,
    _Out_ PKMT_AFFINITY_EX2_BUFFER Buffer2,
    _In_ USHORT Count1,
    _In_ USHORT Count2,
    _In_ USHORT Size1,
    _In_ USHORT Size2,
    _In_ ULONG Seed)
{
    KAFFINITY Combination;
    KAFFINITY Mask1;
    KAFFINITY Mask2;
    ULONG GroupNumber;
    USHORT Pattern;

    RtlFillMemory(Buffer1, sizeof(*Buffer1), (UCHAR)Seed);
    RtlFillMemory(Buffer2, sizeof(*Buffer2), (UCHAR)(0x3C ^ Seed));
    Buffer1->Affinity.Count = Count1;
    Buffer1->Affinity.Size = Size1;
    Buffer1->Affinity.Reserved = 0xC3D2E1F0UL ^ Seed;
    Buffer2->Affinity.Count = Count2;
    Buffer2->Affinity.Size = Size2;
    Buffer2->Affinity.Reserved = 0x5A6B7C8DUL ^ Seed;

    for (GroupNumber = 0; GroupNumber < KMT_AFFINITY_EX2_GROUPS; GroupNumber++)
    {
        Pattern = (USHORT)(Seed + GroupNumber * 0x9E37U);
        Combination = (KAFFINITY)Pattern;
        Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
        Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
        Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;

        if ((Seed & 3) == 0)
        {
            Mask1 = 0;
            Mask2 = 0;
        }
        else if ((Seed & 3) == 1)
        {
            Mask1 = Combination;
            Mask2 = Combination;
        }
        else if ((Seed & 3) == 2)
        {
            Mask1 = Combination;
            Mask2 = ~Combination;
        }
        else
        {
            Mask1 = (KAFFINITY)1 << ((Seed + GroupNumber) % (sizeof(KAFFINITY) * 8));
            Mask2 = (KAFFINITY)1 << ((Seed * 7 + GroupNumber * 13) % (sizeof(KAFFINITY) * 8));
        }

        Buffer1->Affinity.Bitmap[GroupNumber] = Mask1;
        Buffer2->Affinity.Bitmap[GroupNumber] = Mask2;
    }
}

static LOGICAL
KmtBuildOrAffinityEx2Expected(
    _Inout_ PKMT_AFFINITY_EX2_BUFFER Expected,
    _In_ PKMT_AFFINITY_EX2_BUFFER Source1,
    _In_ PKMT_AFFINITY_EX2_BUFFER Source2,
    _In_ USHORT ResultSize)
{
    KAFFINITY ProcessorMask;
    LOGICAL NonEmpty = FALSE;
    ULONG GroupNumber;
    USHORT ResultCount;

    ResultCount = max(Source1->Affinity.Count, Source2->Affinity.Count);
    if (ResultCount > ResultSize)
        ResultCount = ResultSize;

    Expected->Affinity.Count = ResultCount;
    Expected->Affinity.Size = ResultSize;
    Expected->Affinity.Reserved = 0;

    for (GroupNumber = 0; GroupNumber < ResultSize; GroupNumber++)
    {
        ProcessorMask = 0;
        if (GroupNumber < Source1->Affinity.Count)
            ProcessorMask |= Source1->Affinity.Bitmap[GroupNumber];
        if (GroupNumber < Source2->Affinity.Count)
            ProcessorMask |= Source2->Affinity.Bitmap[GroupNumber];
        Expected->Affinity.Bitmap[GroupNumber] = ProcessorMask;
        if (ProcessorMask != 0)
            NonEmpty = TRUE;
    }

    return NonEmpty;
}

static VOID
KmtTestOrAffinityEx2(
    _In_ PKMT_KE_OR_AFFINITY_EX2 OrAffinityEx2)
{
    KMT_AFFINITY_EX2_BUFFER Buffer1;
    KMT_AFFINITY_EX2_BUFFER Buffer2;
    KMT_AFFINITY_EX2_BUFFER Expected;
    KMT_AFFINITY_EX2_BUFFER Result;
    KMT_AFFINITY_EX2_BUFFER Source1;
    KMT_AFFINITY_EX2_BUFFER Source2;
    KAFFINITY ProcessorMask;
    LOGICAL ExpectedLogical;
    LOGICAL LogicalResult;
    ULONG Count1;
    ULONG Count2;
    ULONG CountIndex;
    ULONG GroupNumber;
    ULONG PatternIndex;
    ULONG Seed;
    ULONG SizeIndex;
    USHORT ResultSize;
    static const USHORT Sizes[] =
    {
        0,
        1,
        2,
        3,
        KAFFINITY_EX_INITIALIZED_GROUPS - 1,
        KAFFINITY_EX_INITIALIZED_GROUPS,
        KAFFINITY_EX_INITIALIZED_GROUPS + 1,
        KAFFINITY_EX_STATIC_GROUPS - 1,
        KAFFINITY_EX_STATIC_GROUPS,
        KMT_AFFINITY_EX2_GROUPS - 1,
        KMT_AFFINITY_EX2_GROUPS
    };
    static const USHORT CountPairs[][2] =
    {
        {0, 0},
        {0, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, 0},
        {1, 1},
        {1, 2},
        {2, 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KMT_AFFINITY_EX2_GROUPS - 1, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS - 1},
        {KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS},
        {MAXUSHORT, MAXUSHORT},
        {MAXUSHORT, 1},
        {1, MAXUSHORT}
    };
    static const USHORT ExhaustiveTriples[][3] =
    {
        {0, 0, 0},
        {0, KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, 0, KMT_AFFINITY_EX2_GROUPS},
        {1, 1, 0},
        {1, 1, 1},
        {1, 1, 2},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {MAXUSHORT, MAXUSHORT, 0},
        {MAXUSHORT, MAXUSHORT, KAFFINITY_EX_INITIALIZED_GROUPS},
        {MAXUSHORT, MAXUSHORT, KMT_AFFINITY_EX2_GROUPS},
        {MAXUSHORT, 1, KMT_AFFINITY_EX2_GROUPS},
        {1, MAXUSHORT, KMT_AFFINITY_EX2_GROUPS}
    };

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(CountPairs); CountIndex++)
    {
        for (SizeIndex = 0; SizeIndex < RTL_NUMBER_OF(Sizes); SizeIndex++)
        {
            ResultSize = Sizes[SizeIndex];
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                Seed = (CountIndex << 16) ^ (SizeIndex << 8) ^ PatternIndex;
                KmtInitializeOrAffinityEx2Inputs(&Buffer1, &Buffer2, CountPairs[CountIndex][0], CountPairs[CountIndex][1], Sizes[(CountIndex + SizeIndex) % RTL_NUMBER_OF(Sizes)], Sizes[(CountIndex + SizeIndex + 1) % RTL_NUMBER_OF(Sizes)], Seed);
                Source1 = Buffer1;
                Source2 = Buffer2;

                RtlFillMemory(&Result, sizeof(Result), (UCHAR)(0xA5 ^ Seed));
                Result.Affinity.Size = ResultSize;
                Expected = Result;
                ExpectedLogical = KmtBuildOrAffinityEx2Expected(&Expected, &Source1, &Source2, ResultSize);
                LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Result.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
                ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
                ok_eq_size(RtlCompareMemory(&Result, &Expected, sizeof(Result)), sizeof(Result));

                Buffer1 = Source1;
                Buffer2 = Source2;
                Expected = Source1;
                ExpectedLogical = KmtBuildOrAffinityEx2Expected(&Expected, &Source1, &Source2, Source1.Affinity.Size);
                LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Buffer1.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Expected, sizeof(Buffer1)), sizeof(Buffer1));
                ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));

                Buffer1 = Source1;
                Buffer2 = Source2;
                Expected = Source2;
                ExpectedLogical = KmtBuildOrAffinityEx2Expected(&Expected, &Source1, &Source2, Source2.Affinity.Size);
                LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
                ok_eq_size(RtlCompareMemory(&Buffer2, &Expected, sizeof(Buffer2)), sizeof(Buffer2));

                Buffer1 = Source1;
                Expected = Source1;
                ExpectedLogical = KmtBuildOrAffinityEx2Expected(&Expected, &Source1, &Source1, Source1.Affinity.Size);
                LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer1.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Expected, sizeof(Buffer1)), sizeof(Buffer1));
            }
        }
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(ExhaustiveTriples); CountIndex++)
    {
        for (PatternIndex = 0; PatternIndex <= MAXUSHORT; PatternIndex++)
        {
            Seed = (CountIndex << 16) ^ PatternIndex;
            KmtInitializeOrAffinityEx2Inputs(&Buffer1, &Buffer2, ExhaustiveTriples[CountIndex][0], ExhaustiveTriples[CountIndex][1], (USHORT)((PatternIndex + CountIndex) % (KMT_AFFINITY_EX2_GROUPS + 1)), (USHORT)((PatternIndex + CountIndex + 1) % (KMT_AFFINITY_EX2_GROUPS + 1)), Seed);
            Source1 = Buffer1;
            Source2 = Buffer2;
            RtlFillMemory(&Result, sizeof(Result), (UCHAR)(0xA5 ^ Seed));
            Result.Affinity.Size = ExhaustiveTriples[CountIndex][2];
            Expected = Result;
            ExpectedLogical = KmtBuildOrAffinityEx2Expected(&Expected, &Source1, &Source2, Result.Affinity.Size);
            LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Result.Affinity);
            ok_eq_ulong(LogicalResult, ExpectedLogical);
            ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
            ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
            ok_eq_size(RtlCompareMemory(&Result, &Expected, sizeof(Result)), sizeof(Result));
        }
    }

    for (Count1 = 0; Count1 <= KMT_AFFINITY_EX2_GROUPS; Count1++)
    {
        for (Count2 = 0; Count2 <= KMT_AFFINITY_EX2_GROUPS; Count2++)
        {
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                Seed = (Count1 << 16) ^ (Count2 << 8) ^ PatternIndex;
                KmtInitializeOrAffinityEx2Inputs(&Buffer1, &Buffer2, (USHORT)Count1, (USHORT)Count2, (USHORT)((Count2 + 1) % (KMT_AFFINITY_EX2_GROUPS + 1)), (USHORT)((Count1 + 1) % (KMT_AFFINITY_EX2_GROUPS + 1)), Seed);
                Source1 = Buffer1;
                Source2 = Buffer2;
                ExpectedLogical = FALSE;
                for (GroupNumber = 0; GroupNumber < max(Count1, Count2); GroupNumber++)
                {
                    ProcessorMask = 0;
                    if (GroupNumber < Count1)
                        ProcessorMask |= Source1.Affinity.Bitmap[GroupNumber];
                    if (GroupNumber < Count2)
                        ProcessorMask |= Source2.Affinity.Bitmap[GroupNumber];
                    if (ProcessorMask != 0)
                        ExpectedLogical = TRUE;
                }
                LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, NULL);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
                ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
            }
        }
    }

    RtlZeroMemory(&Buffer1, sizeof(Buffer1));
    RtlZeroMemory(&Buffer2, sizeof(Buffer2));
    Buffer1.Affinity.Count = KMT_AFFINITY_EX2_GROUPS;
    Buffer1.Affinity.Size = 1;
    Buffer1.Affinity.Reserved = MAXULONG;
    Buffer2.Affinity.Count = KMT_AFFINITY_EX2_GROUPS - 1;
    Buffer2.Affinity.Size = MAXUSHORT;
    Buffer2.Affinity.Reserved = MAXULONG;
    Buffer1.Affinity.Bitmap[KMT_AFFINITY_EX2_GROUPS - 1] = (KAFFINITY)0x8000000000000001ULL;
    Source1 = Buffer1;
    Source2 = Buffer2;
    LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, NULL);
    ok_eq_ulong(LogicalResult, TRUE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));

    RtlFillMemory(&Result, sizeof(Result), 0xA5);
    Result.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
    Expected = Result;
    ExpectedLogical = KmtBuildOrAffinityEx2Expected(&Expected, &Source1, &Source2, Result.Affinity.Size);
    LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Result.Affinity);
    ok_eq_ulong(LogicalResult, ExpectedLogical);
    ok_eq_ulong(LogicalResult, FALSE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
    ok_eq_size(RtlCompareMemory(&Result, &Expected, sizeof(Result)), sizeof(Result));

    RtlFillMemory(&Result, sizeof(Result), 0xA5);
    Result.Affinity.Size = KMT_AFFINITY_EX2_GROUPS;
    Expected = Result;
    ExpectedLogical = KmtBuildOrAffinityEx2Expected(&Expected, &Source1, &Source2, Result.Affinity.Size);
    LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Result.Affinity);
    ok_eq_ulong(LogicalResult, ExpectedLogical);
    ok_eq_ulong(LogicalResult, TRUE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
    ok_eq_size(RtlCompareMemory(&Result, &Expected, sizeof(Result)), sizeof(Result));

    Buffer1.Affinity.Count = KMT_AFFINITY_EX2_GROUPS - 1;
    Source1 = Buffer1;
    LogicalResult = OrAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, NULL);
    ok_eq_ulong(LogicalResult, FALSE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
}

static VOID
KmtInitializeSubtractAffinityEx2Inputs(
    _Out_ PKMT_AFFINITY_EX2_BUFFER Buffer1,
    _Out_ PKMT_AFFINITY_EX2_BUFFER Buffer2,
    _In_ USHORT Count1,
    _In_ USHORT Count2,
    _In_ USHORT Size1,
    _In_ USHORT Size2,
    _In_ ULONG Seed)
{
    KAFFINITY Combination;
    KAFFINITY Mask1;
    KAFFINITY Mask2;
    ULONG GroupNumber;
    USHORT Pattern;

    RtlFillMemory(Buffer1, sizeof(*Buffer1), (UCHAR)Seed);
    RtlFillMemory(Buffer2, sizeof(*Buffer2), (UCHAR)(0x5A ^ Seed));
    Buffer1->Affinity.Count = Count1;
    Buffer1->Affinity.Size = Size1;
    Buffer1->Affinity.Reserved = 0xC3D2E1F0UL ^ Seed;
    Buffer2->Affinity.Count = Count2;
    Buffer2->Affinity.Size = Size2;
    Buffer2->Affinity.Reserved = 0x0F1E2D3CUL ^ Seed;

    for (GroupNumber = 0; GroupNumber < KMT_AFFINITY_EX2_GROUPS; GroupNumber++)
    {
        Pattern = (USHORT)(Seed + GroupNumber * 0x9E37U);
        Combination = (KAFFINITY)Pattern;
        Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
        Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
        Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;

        if ((Seed & 3) == 0)
        {
            Mask1 = 0;
            Mask2 = 0;
        }
        else if ((Seed & 3) == 1)
        {
            Mask1 = Combination;
            Mask2 = Combination;
        }
        else if ((Seed & 3) == 2)
        {
            Mask1 = Combination;
            Mask2 = ~Combination;
        }
        else
        {
            Mask1 = (KAFFINITY)1 << ((Seed + GroupNumber) % (sizeof(KAFFINITY) * 8));
            Mask2 = (GroupNumber & 1) ? Mask1 : (KAFFINITY)1 << ((Seed * 7 + GroupNumber * 13) % (sizeof(KAFFINITY) * 8));
        }

        Buffer1->Affinity.Bitmap[GroupNumber] = Mask1;
        Buffer2->Affinity.Bitmap[GroupNumber] = Mask2;
    }
}

static LOGICAL
KmtBuildSubtractAffinityEx2Expected(
    _Inout_ PKMT_AFFINITY_EX2_BUFFER Expected,
    _In_ PKMT_AFFINITY_EX2_BUFFER Source1,
    _In_ PKMT_AFFINITY_EX2_BUFFER Source2,
    _In_ USHORT ResultSize)
{
    KAFFINITY ProcessorMask;
    LOGICAL NonEmpty = FALSE;
    ULONG GroupNumber;
    USHORT ResultCount;

    ResultCount = Source1->Affinity.Count;
    if (ResultCount > ResultSize)
        ResultCount = ResultSize;

    Expected->Affinity.Count = ResultCount;
    Expected->Affinity.Size = ResultSize;
    Expected->Affinity.Reserved = 0;

    for (GroupNumber = 0; GroupNumber < ResultSize; GroupNumber++)
    {
        ProcessorMask = 0;
        if (GroupNumber < ResultCount)
        {
            ProcessorMask = Source1->Affinity.Bitmap[GroupNumber];
            if (GroupNumber < Source2->Affinity.Count)
                ProcessorMask &= ~Source2->Affinity.Bitmap[GroupNumber];
        }
        Expected->Affinity.Bitmap[GroupNumber] = ProcessorMask;
        if (ProcessorMask != 0)
            NonEmpty = TRUE;
    }

    return NonEmpty;
}

static VOID
KmtTestSubtractAffinityEx2(
    _In_ PKMT_KE_SUBTRACT_AFFINITY_EX2 SubtractAffinityEx2)
{
    KMT_AFFINITY_EX2_BUFFER Buffer1;
    KMT_AFFINITY_EX2_BUFFER Buffer2;
    KMT_AFFINITY_EX2_BUFFER Expected;
    KMT_AFFINITY_EX2_BUFFER Result;
    KMT_AFFINITY_EX2_BUFFER Source1;
    KMT_AFFINITY_EX2_BUFFER Source2;
    KAFFINITY ProcessorMask;
    LOGICAL ExpectedLogical;
    LOGICAL LogicalResult;
    ULONG Count1;
    ULONG Count2;
    ULONG CountIndex;
    ULONG GroupNumber;
    ULONG PatternIndex;
    ULONG Seed;
    ULONG SizeIndex;
    USHORT ResultSize;
    static const USHORT Sizes[] =
    {
        0,
        1,
        2,
        3,
        KAFFINITY_EX_INITIALIZED_GROUPS - 1,
        KAFFINITY_EX_INITIALIZED_GROUPS,
        KAFFINITY_EX_INITIALIZED_GROUPS + 1,
        KAFFINITY_EX_STATIC_GROUPS - 1,
        KAFFINITY_EX_STATIC_GROUPS,
        KMT_AFFINITY_EX2_GROUPS - 1,
        KMT_AFFINITY_EX2_GROUPS
    };
    static const USHORT CountPairs[][2] =
    {
        {0, 0},
        {0, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, 0},
        {1, 1},
        {1, 2},
        {2, 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KMT_AFFINITY_EX2_GROUPS - 1, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS - 1},
        {KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS},
        {MAXUSHORT, MAXUSHORT},
        {MAXUSHORT, 1},
        {1, MAXUSHORT}
    };
    static const USHORT ExhaustiveTriples[][3] =
    {
        {0, 0, 0},
        {0, KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, 0, KMT_AFFINITY_EX2_GROUPS},
        {1, 1, 0},
        {1, 1, 1},
        {1, 1, 2},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {MAXUSHORT, MAXUSHORT, 0},
        {MAXUSHORT, MAXUSHORT, KAFFINITY_EX_INITIALIZED_GROUPS},
        {MAXUSHORT, MAXUSHORT, KMT_AFFINITY_EX2_GROUPS},
        {MAXUSHORT, 1, KMT_AFFINITY_EX2_GROUPS},
        {1, MAXUSHORT, KMT_AFFINITY_EX2_GROUPS}
    };

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(CountPairs); CountIndex++)
    {
        for (SizeIndex = 0; SizeIndex < RTL_NUMBER_OF(Sizes); SizeIndex++)
        {
            ResultSize = Sizes[SizeIndex];
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                Seed = (CountIndex << 16) ^ (SizeIndex << 8) ^ PatternIndex;
                KmtInitializeSubtractAffinityEx2Inputs(&Buffer1, &Buffer2, CountPairs[CountIndex][0], CountPairs[CountIndex][1], Sizes[(CountIndex + SizeIndex) % RTL_NUMBER_OF(Sizes)], Sizes[(CountIndex + SizeIndex + 1) % RTL_NUMBER_OF(Sizes)], Seed);
                Source1 = Buffer1;
                Source2 = Buffer2;

                RtlFillMemory(&Result, sizeof(Result), (UCHAR)(0xA5 ^ Seed));
                Result.Affinity.Size = ResultSize;
                Expected = Result;
                ExpectedLogical = KmtBuildSubtractAffinityEx2Expected(&Expected, &Source1, &Source2, ResultSize);
                LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Result.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
                ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
                ok_eq_size(RtlCompareMemory(&Result, &Expected, sizeof(Result)), sizeof(Result));

                Buffer1 = Source1;
                Buffer2 = Source2;
                Expected = Source1;
                ExpectedLogical = KmtBuildSubtractAffinityEx2Expected(&Expected, &Source1, &Source2, Source1.Affinity.Size);
                LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Buffer1.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Expected, sizeof(Buffer1)), sizeof(Buffer1));
                ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));

                Buffer1 = Source1;
                Buffer2 = Source2;
                Expected = Source2;
                ExpectedLogical = KmtBuildSubtractAffinityEx2Expected(&Expected, &Source1, &Source2, Source2.Affinity.Size);
                LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
                ok_eq_size(RtlCompareMemory(&Buffer2, &Expected, sizeof(Buffer2)), sizeof(Buffer2));

                Buffer1 = Source1;
                Expected = Source1;
                ExpectedLogical = KmtBuildSubtractAffinityEx2Expected(&Expected, &Source1, &Source1, Source1.Affinity.Size);
                LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer1.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Expected, sizeof(Buffer1)), sizeof(Buffer1));
            }
        }
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(ExhaustiveTriples); CountIndex++)
    {
        for (PatternIndex = 0; PatternIndex <= MAXUSHORT; PatternIndex++)
        {
            Seed = (CountIndex << 16) ^ PatternIndex;
            KmtInitializeSubtractAffinityEx2Inputs(&Buffer1, &Buffer2, ExhaustiveTriples[CountIndex][0], ExhaustiveTriples[CountIndex][1], (USHORT)((PatternIndex + CountIndex) % (KMT_AFFINITY_EX2_GROUPS + 1)), (USHORT)((PatternIndex + CountIndex + 1) % (KMT_AFFINITY_EX2_GROUPS + 1)), Seed);
            Source1 = Buffer1;
            Source2 = Buffer2;
            RtlFillMemory(&Result, sizeof(Result), (UCHAR)(0xA5 ^ Seed));
            Result.Affinity.Size = ExhaustiveTriples[CountIndex][2];
            Expected = Result;
            ExpectedLogical = KmtBuildSubtractAffinityEx2Expected(&Expected, &Source1, &Source2, Result.Affinity.Size);
            LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Result.Affinity);
            ok_eq_ulong(LogicalResult, ExpectedLogical);
            ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
            ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
            ok_eq_size(RtlCompareMemory(&Result, &Expected, sizeof(Result)), sizeof(Result));
        }
    }

    for (Count1 = 0; Count1 <= KMT_AFFINITY_EX2_GROUPS; Count1++)
    {
        for (Count2 = 0; Count2 <= KMT_AFFINITY_EX2_GROUPS; Count2++)
        {
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                Seed = (Count1 << 16) ^ (Count2 << 8) ^ PatternIndex;
                KmtInitializeSubtractAffinityEx2Inputs(&Buffer1, &Buffer2, (USHORT)Count1, (USHORT)Count2, (USHORT)((Count2 + 1) % (KMT_AFFINITY_EX2_GROUPS + 1)), (USHORT)((Count1 + 1) % (KMT_AFFINITY_EX2_GROUPS + 1)), Seed);
                Source1 = Buffer1;
                Source2 = Buffer2;
                ExpectedLogical = FALSE;
                for (GroupNumber = 0; GroupNumber < Count1; GroupNumber++)
                {
                    ProcessorMask = Source1.Affinity.Bitmap[GroupNumber];
                    if (GroupNumber < Count2)
                        ProcessorMask &= ~Source2.Affinity.Bitmap[GroupNumber];
                    if (ProcessorMask != 0)
                        ExpectedLogical = TRUE;
                }
                LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, NULL);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
                ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
            }
        }
    }

    RtlZeroMemory(&Buffer1, sizeof(Buffer1));
    RtlZeroMemory(&Buffer2, sizeof(Buffer2));
    Buffer1.Affinity.Count = KMT_AFFINITY_EX2_GROUPS;
    Buffer1.Affinity.Size = 1;
    Buffer1.Affinity.Reserved = MAXULONG;
    Buffer2.Affinity.Count = KMT_AFFINITY_EX2_GROUPS - 1;
    Buffer2.Affinity.Size = MAXUSHORT;
    Buffer2.Affinity.Reserved = MAXULONG;
    Buffer1.Affinity.Bitmap[KMT_AFFINITY_EX2_GROUPS - 1] = (KAFFINITY)0x8000000000000001ULL;
    Source1 = Buffer1;
    Source2 = Buffer2;
    LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, NULL);
    ok_eq_ulong(LogicalResult, TRUE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));

    RtlFillMemory(&Result, sizeof(Result), 0xA5);
    Result.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
    Expected = Result;
    ExpectedLogical = KmtBuildSubtractAffinityEx2Expected(&Expected, &Source1, &Source2, Result.Affinity.Size);
    LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Result.Affinity);
    ok_eq_ulong(LogicalResult, ExpectedLogical);
    ok_eq_ulong(LogicalResult, FALSE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
    ok_eq_size(RtlCompareMemory(&Result, &Expected, sizeof(Result)), sizeof(Result));

    RtlFillMemory(&Result, sizeof(Result), 0xA5);
    Result.Affinity.Size = KMT_AFFINITY_EX2_GROUPS;
    Expected = Result;
    ExpectedLogical = KmtBuildSubtractAffinityEx2Expected(&Expected, &Source1, &Source2, Result.Affinity.Size);
    LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, (PKAFFINITY_EX)&Result.Affinity);
    ok_eq_ulong(LogicalResult, ExpectedLogical);
    ok_eq_ulong(LogicalResult, TRUE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
    ok_eq_size(RtlCompareMemory(&Result, &Expected, sizeof(Result)), sizeof(Result));

    Buffer2.Affinity.Count = KMT_AFFINITY_EX2_GROUPS;
    Buffer2.Affinity.Bitmap[KMT_AFFINITY_EX2_GROUPS - 1] = Buffer1.Affinity.Bitmap[KMT_AFFINITY_EX2_GROUPS - 1];
    Source2 = Buffer2;
    LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, NULL);
    ok_eq_ulong(LogicalResult, FALSE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));

    Buffer1.Affinity.Count = KMT_AFFINITY_EX2_GROUPS - 1;
    Buffer2.Affinity.Count = KMT_AFFINITY_EX2_GROUPS - 1;
    Source1 = Buffer1;
    Source2 = Buffer2;
    LogicalResult = SubtractAffinityEx2((PKAFFINITY_EX)&Buffer1.Affinity, (PKAFFINITY_EX)&Buffer2.Affinity, NULL);
    ok_eq_ulong(LogicalResult, FALSE);
    ok_eq_size(RtlCompareMemory(&Buffer1, &Source1, sizeof(Buffer1)), sizeof(Buffer1));
    ok_eq_size(RtlCompareMemory(&Buffer2, &Source2, sizeof(Buffer2)), sizeof(Buffer2));
}

static ULONG_PTR
NTAPI
KmtInterlockedSetProcessorAffinityExIpiWorker(
    _In_ ULONG_PTR Argument)
{
    PKMT_INTERLOCKED_SET_AFFINITY_CONTEXT Context = (PKMT_INTERLOCKED_SET_AFFINITY_CONTEXT)Argument;

    if (Context->SetProcessorAffinityEx(Context->Affinity, KeGetCurrentProcessorNumberEx(NULL)))
        InterlockedIncrement(&Context->TrueCount);

    return 0;
}

static VOID
KmtTestInterlockedSetProcessorAffinityEx(
    _In_ PKMT_KE_INTERLOCKED_SET_PROCESSOR_AFFINITY_EX SetProcessorAffinityEx,
    _In_ PKMT_KE_GET_PROCESSOR_NUMBER_FROM_INDEX GetProcessorNumberFromIndex)
{
    KMT_AFFINITY_EX2_BUFFER Buffer;
    KMT_AFFINITY_EX2_BUFFER Expected;
    KMT_INTERLOCKED_SET_AFFINITY_CONTEXT Context;
    PROCESSOR_NUMBER ProcessorNumber;
    KAFFINITY ProcessorMask;
    LOGICAL ExpectedLogical;
    LOGICAL LogicalResult;
    NTSTATUS Status;
    ULONG ActiveCount;
    ULONG CountIndex;
    ULONG GroupNumber;
    ULONG PatternIndex;
    ULONG ProcessorIndex;
    ULONG Round;
    ULONG Seed;
    ULONG SizeIndex;
    ULONG_PTR IpiResult;
    static const USHORT Counts[] =
    {
        0,
        1,
        2,
        KAFFINITY_EX_INITIALIZED_GROUPS - 1,
        KAFFINITY_EX_INITIALIZED_GROUPS,
        KAFFINITY_EX_INITIALIZED_GROUPS + 1,
        KMT_AFFINITY_EX2_GROUPS,
        MAXUSHORT
    };
    static const USHORT Sizes[] =
    {
        0,
        1,
        2,
        3,
        KAFFINITY_EX_INITIALIZED_GROUPS - 1,
        KAFFINITY_EX_INITIALIZED_GROUPS,
        KAFFINITY_EX_INITIALIZED_GROUPS + 1,
        KAFFINITY_EX_STATIC_GROUPS - 1,
        KAFFINITY_EX_STATIC_GROUPS,
        KMT_AFFINITY_EX2_GROUPS - 1,
        KMT_AFFINITY_EX2_GROUPS,
        MAXUSHORT
    };

    ActiveCount = KeQueryActiveProcessorCount(NULL);
    ok(ActiveCount != 0, "No active processors reported\n");

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        Status = GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status) || ProcessorNumber.Group >= KMT_AFFINITY_EX2_GROUPS)
            continue;

        ProcessorMask = (KAFFINITY)1 << ProcessorNumber.Number;
        for (SizeIndex = 0; SizeIndex < RTL_NUMBER_OF(Sizes); SizeIndex++)
        {
            for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(Counts); CountIndex++)
            {
                for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
                {
                    Seed = (ProcessorIndex << 24) ^ (SizeIndex << 16) ^ (CountIndex << 8) ^ PatternIndex;
                    RtlFillMemory(&Buffer, sizeof(Buffer), (UCHAR)(0xA5 ^ Seed));
                    Buffer.Affinity.Count = Counts[CountIndex];
                    Buffer.Affinity.Size = Sizes[SizeIndex];
                    Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ Seed;
                    for (GroupNumber = 0; GroupNumber < KMT_AFFINITY_EX2_GROUPS; GroupNumber++)
                        Buffer.Affinity.Bitmap[GroupNumber] = ((KAFFINITY)(Seed + GroupNumber * 0x9E3779B9UL) << 32) | (ULONG)(~Seed + GroupNumber * 0x7F4A7C15UL);

                    Expected = Buffer;
                    ExpectedLogical = FALSE;
                    if (ProcessorNumber.Group < Buffer.Affinity.Size)
                    {
                        ExpectedLogical = (Expected.Affinity.Bitmap[ProcessorNumber.Group] & ProcessorMask) != 0;
                        Expected.Affinity.Bitmap[ProcessorNumber.Group] |= ProcessorMask;
                    }

                    LogicalResult = SetProcessorAffinityEx((PKAFFINITY_EX)&Buffer.Affinity, ProcessorIndex);
                    ok_eq_ulong(LogicalResult, ExpectedLogical);
                    ok_eq_size(RtlCompareMemory(&Buffer, &Expected, sizeof(Buffer)), sizeof(Buffer));

                    LogicalResult = SetProcessorAffinityEx((PKAFFINITY_EX)&Buffer.Affinity, ProcessorIndex);
                    ok_eq_ulong(LogicalResult, ProcessorNumber.Group < Buffer.Affinity.Size);
                    ok_eq_size(RtlCompareMemory(&Buffer, &Expected, sizeof(Buffer)), sizeof(Buffer));
                }
            }
        }
    }

    RtlFillMemory(&Buffer, sizeof(Buffer), 0xA5);
    Buffer.Affinity.Count = MAXUSHORT;
    Buffer.Affinity.Size = KMT_AFFINITY_EX2_GROUPS;
    Buffer.Affinity.Reserved = 0xC3D2E1F0UL;
    RtlZeroMemory(Buffer.Affinity.Bitmap, sizeof(Buffer.Affinity.Bitmap));
    Expected = Buffer;
    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        Status = GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status) && ProcessorNumber.Group < KMT_AFFINITY_EX2_GROUPS)
            Expected.Affinity.Bitmap[ProcessorNumber.Group] |= (KAFFINITY)1 << ProcessorNumber.Number;
    }

    Context.SetProcessorAffinityEx = SetProcessorAffinityEx;
    Context.Affinity = (PKAFFINITY_EX)&Buffer.Affinity;
    for (Round = 0; Round < 1024; Round++)
    {
        RtlZeroMemory(Buffer.Affinity.Bitmap, sizeof(Buffer.Affinity.Bitmap));
        InterlockedExchange(&Context.TrueCount, 0);
        IpiResult = KeIpiGenericCall(KmtInterlockedSetProcessorAffinityExIpiWorker, (ULONG_PTR)&Context);
        ok_eq_ulongptr(IpiResult, 0);
        ok_eq_long(Context.TrueCount, 0);
        ok_eq_size(RtlCompareMemory(&Buffer, &Expected, sizeof(Buffer)), sizeof(Buffer));

        InterlockedExchange(&Context.TrueCount, 0);
        IpiResult = KeIpiGenericCall(KmtInterlockedSetProcessorAffinityExIpiWorker, (ULONG_PTR)&Context);
        ok_eq_ulongptr(IpiResult, 0);
        ok_eq_long(Context.TrueCount, (LONG)ActiveCount);
        ok_eq_size(RtlCompareMemory(&Buffer, &Expected, sizeof(Buffer)), sizeof(Buffer));
    }
}
#endif

START_TEST(KeArm64AffinityEx)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64AffinityEx is ARM64-only\n");
#else
    KAFFINITY_EX Affinity;
    KAFFINITY_EX Result;
    KAFFINITY_EX Source;
    KMT_AFFINITY_EX2_BUFFER AffinityEx2Buffer;
    KMT_AFFINITY_EX2_BUFFER AffinityEx2Source;
    KMT_AFFINITY_EX2_BUFFER AffinityEx2Buffer2;
    KMT_AFFINITY_EX2_BUFFER AffinityEx2Source2;
    KMT_AFFINITY_EX2_BUFFER AffinityEx2Result;
    KMT_AFFINITY_EX2_BUFFER AffinityEx2ResultExpected;
    KMT_AFFINITY_EX2_BUFFER AffinityEx2AliasExpected;
    KMT_AFFINITY_ENUMERATION_CONTEXT_BUFFER EnumerationBuffer;
    KMT_AFFINITY_ENUMERATION_CONTEXT_BUFFER EnumerationSource;
    KMT_GROUP_AFFINITY_BUFFER GroupBuffer;
    KMT_GROUP_AFFINITY_BUFFER GroupBufferSource;
    KMT_GROUP_AFFINITY_BUFFER GroupResultBuffer;
    KMT_GROUP_AFFINITY_BUFFER GroupResultExpected;
    KMT_PROCESSOR_INDEX_BUFFER ProcessorIndexBuffer;
    KMT_PROCESSOR_INDEX_BUFFER ProcessorIndexSource;
    ULONG ActiveCount;
    ULONG BitNumber;
    KAFFINITY Combination;
    KAFFINITY CombinationLimit;
    KAFFINITY RemainingAffinity;
    GROUP_AFFINITY GroupAffinity;
    GROUP_AFFINITY GroupSource;
    ULONG HighestIndex;
    ULONG LowestIndex;
    ULONG OtherIndex;
    ULONG ProcessorIndex;
    ULONG ExpectedProcessorIndex;
    PKMT_KE_AND_AFFINITY_EX AndAffinityEx;
    PKMT_KE_AND_AFFINITY_EX2 AndAffinityEx2;
    PKMT_KE_AND_GROUP_AFFINITY_EX AndGroupAffinityEx;
    PKMT_KE_ADD_PROCESSOR_GROUP_AFFINITY AddProcessorGroupAffinity;
    PKMT_KE_CHECK_PROCESSOR_GROUP_AFFINITY CheckProcessorGroupAffinity;
    PKMT_KE_COMPLEMENT_AFFINITY_EX ComplementAffinityEx;
    PKMT_KE_COMPLEMENT_AFFINITY_EX2 ComplementAffinityEx2;
    PKMT_KE_COPY_AFFINITY_EX CopyAffinityEx;
    PKMT_KE_COPY_AFFINITY_EX2 CopyAffinityEx2;
    PKMT_KE_COUNT_SET_BITS_AFFINITY_EX CountSetBitsAffinityEx;
    PKMT_KE_COUNT_SET_BITS_GROUP_AFFINITY CountSetBitsGroupAffinity;
    PKMT_KE_ENUMERATE_NEXT_PROCESSOR EnumerateNextProcessor;
    PKMT_KE_FIND_FIRST_SET_LEFT_AFFINITY_EX FindFirstSetLeftAffinityEx;
    PKMT_KE_FIND_FIRST_SET_LEFT_GROUP_AFFINITY FindFirstSetLeftGroupAffinity;
    PKMT_KE_FIND_FIRST_SET_RIGHT_AFFINITY_EX FindFirstSetRightAffinityEx;
    PKMT_KE_FIND_FIRST_SET_RIGHT_GROUP_AFFINITY FindFirstSetRightGroupAffinity;
    PKMT_KE_FIRST_GROUP_AFFINITY_EX FirstGroupAffinityEx;
    PKMT_KE_INITIALIZE_AFFINITY_EX2 InitializeAffinityEx2;
    PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT InitializeEnumerationContext;
    PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT_FROM_AFFINITY InitializeEnumerationContextFromAffinity;
    PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT_FROM_GROUP InitializeEnumerationContextFromGroup;
    PKMT_KE_INTERLOCKED_SET_PROCESSOR_AFFINITY_EX InterlockedSetProcessorAffinityEx;
    PKMT_KE_GET_PROCESSOR_INDEX_FROM_NUMBER GetProcessorIndexFromNumber;
    PKMT_KE_GET_PROCESSOR_NUMBER_FROM_INDEX GetProcessorNumberFromIndex;
    PKMT_KE_IS_EQUAL_AFFINITY_EX IsEqualAffinityEx;
    PKMT_KE_IS_SINGLE_GROUP_AFFINITY_EX IsSingleGroupAffinityEx;
    PKMT_KE_IS_SUBSET_AFFINITY_EX IsSubsetAffinityEx;
    PKMT_KE_OR_AFFINITY_EX OrAffinityEx;
    PKMT_KE_OR_AFFINITY_EX2 OrAffinityEx2;
    PKMT_KE_PROCESSOR_GROUP_AFFINITY ProcessorGroupAffinity;
    PKMT_KE_REMOVE_PROCESSOR_GROUP_AFFINITY RemoveProcessorGroupAffinity;
    PKMT_KE_SIZE_OF_AFFINITY_EX SizeOfAffinityEx;
    PKMT_KE_SUBTRACT_AFFINITY_EX SubtractAffinityEx;
    PKMT_KE_SUBTRACT_AFFINITY_EX2 SubtractAffinityEx2;
    PROCESSOR_NUMBER HighestProcessorNumber;
    PROCESSOR_NUMBER LowestProcessorNumber;
    PROCESSOR_NUMBER ProcessorNumber;
    NTSTATUS EnumerationStatus;
    UNICODE_STRING Name;
    USHORT ExpectedGroup;
    USHORT GroupNumber;
    USHORT OtherGroup;
    USHORT Size;
    static const KAFFINITY EnumerationMasks[] =
    {
        0,
        1,
        (KAFFINITY)1 << 63,
        ~(KAFFINITY)0,
        (KAFFINITY)0x5555555555555555ULL,
        (KAFFINITY)0xAAAAAAAAAAAAAAAAULL,
        (KAFFINITY)0x0123456789ABCDEFULL,
        (KAFFINITY)0xFEDCBA9876543210ULL
    };
    static const USHORT EnumerationCounts[] =
    {
        0,
        1,
        2,
        3,
        KAFFINITY_EX_INITIALIZED_GROUPS
    };
    static const USHORT GroupAffinityCounts[] =
    {
        0,
        1,
        2,
        3,
        KMT_AFFINITY_EX2_GROUPS / 2 - 1,
        KMT_AFFINITY_EX2_GROUPS / 2,
        KMT_AFFINITY_EX2_GROUPS - 1,
        KMT_AFFINITY_EX2_GROUPS
    };
    static const USHORT ComplementAffinityCounts[] =
    {
        0,
        1,
        2,
        3,
        KAFFINITY_EX_INITIALIZED_GROUPS - 1,
        KAFFINITY_EX_INITIALIZED_GROUPS,
        KAFFINITY_EX_INITIALIZED_GROUPS + 1,
        KAFFINITY_EX_STATIC_GROUPS - 1,
        KAFFINITY_EX_STATIC_GROUPS,
        KMT_AFFINITY_EX2_GROUPS - 1,
        KMT_AFFINITY_EX2_GROUPS,
        MAXUSHORT
    };
    static const USHORT SubtractAffinityExhaustivePairs[][2] =
    {
        {1, 0},
        {1, 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {MAXUSHORT, 0},
        {0, MAXUSHORT},
        {MAXUSHORT, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, MAXUSHORT},
        {MAXUSHORT, MAXUSHORT}
    };
    static const USHORT AffinityEx2Sizes[] =
    {
        0,
        1,
        2,
        3,
        KAFFINITY_EX_INITIALIZED_GROUPS - 1,
        KAFFINITY_EX_INITIALIZED_GROUPS,
        KAFFINITY_EX_INITIALIZED_GROUPS + 1,
        KAFFINITY_EX_STATIC_GROUPS - 1,
        KAFFINITY_EX_STATIC_GROUPS,
        KMT_AFFINITY_EX2_GROUPS - 1,
        KMT_AFFINITY_EX2_GROUPS
    };
    static const USHORT CopyAffinityEx2ExhaustivePairs[][2] =
    {
        {0, 0},
        {0, KMT_AFFINITY_EX2_GROUPS},
        {1, 0},
        {1, 1},
        {1, 2},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {MAXUSHORT, 0},
        {MAXUSHORT, KAFFINITY_EX_INITIALIZED_GROUPS},
        {MAXUSHORT, KMT_AFFINITY_EX2_GROUPS}
    };
    static const USHORT ComplementAffinityEx2ExhaustivePairs[][2] =
    {
        {0, 0},
        {0, KMT_AFFINITY_EX2_GROUPS},
        {1, 0},
        {1, 1},
        {1, 2},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {MAXUSHORT, 0},
        {MAXUSHORT, KAFFINITY_EX_INITIALIZED_GROUPS},
        {MAXUSHORT, KMT_AFFINITY_EX2_GROUPS}
    };
    static const USHORT AndAffinityEx2ExhaustiveTriples[][3] =
    {
        {0, 0, 0},
        {0, KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, 0, KMT_AFFINITY_EX2_GROUPS},
        {1, 1, 0},
        {1, 1, 1},
        {1, 1, 2},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {MAXUSHORT, MAXUSHORT, 0},
        {MAXUSHORT, MAXUSHORT, KAFFINITY_EX_INITIALIZED_GROUPS},
        {MAXUSHORT, MAXUSHORT, KMT_AFFINITY_EX2_GROUPS},
        {MAXUSHORT, 1, KMT_AFFINITY_EX2_GROUPS},
        {1, MAXUSHORT, KMT_AFFINITY_EX2_GROUPS}
    };
    static const USHORT AndAffinityEx2CountPairs[][2] =
    {
        {0, 0},
        {0, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, 0},
        {1, 1},
        {1, 2},
        {2, 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS - 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS - 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS},
        {KAFFINITY_EX_INITIALIZED_GROUPS + 1, KAFFINITY_EX_INITIALIZED_GROUPS + 1},
        {KMT_AFFINITY_EX2_GROUPS - 1, KMT_AFFINITY_EX2_GROUPS},
        {KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS - 1},
        {KMT_AFFINITY_EX2_GROUPS, KMT_AFFINITY_EX2_GROUPS},
        {MAXUSHORT, MAXUSHORT},
        {MAXUSHORT, 1},
        {1, MAXUSHORT}
    };
    ULONG CountIndex;
    ULONG CountIndex2;
    ULONG CountResult;
    ULONG ExpectedCount;
    ULONG MaskIndex;
    ULONG PatternIndex;
    ULONG ProbeGroup;
    ULONG SizeIndex;
    ULONG GroupValue;
    KAFFINITY ExpectedMask;
    USHORT Pattern;
    USHORT AffinityCount1;
    USHORT AffinityCount2;
    USHORT CommonCount;
    USHORT ResultCount;
    USHORT ResultSize;
    USHORT AliasCount;
    USHORT AliasSize;
    LOGICAL ExpectedLogical;
    LOGICAL LogicalResult;

    ok_eq_size(sizeof(Affinity), (SIZE_T)264);
    ok_eq_size(FIELD_OFFSET(KAFFINITY_EX, Count), (SIZE_T)0);
    ok_eq_size(FIELD_OFFSET(KAFFINITY_EX, Size), (SIZE_T)2);
    ok_eq_size(FIELD_OFFSET(KAFFINITY_EX, Reserved), (SIZE_T)4);
    ok_eq_size(FIELD_OFFSET(KAFFINITY_EX, Bitmap), (SIZE_T)8);
    ok_eq_size(FIELD_OFFSET(KAFFINITY_EX, StaticBitmap), (SIZE_T)8);

    RtlFillMemory(&Affinity, sizeof(Affinity), 0xA5);
    KeInitializeAffinityEx(&Affinity);
    ok_eq_uint(Affinity.Count, 1);
    ok_eq_uint(Affinity.Size, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulong(Affinity.Reserved, 0);
    ok_eq_ulonglong(KeQueryGroupAffinityEx(&Affinity, 0), 0);
    ok_eq_ulonglong(Affinity.StaticBitmap[KAFFINITY_EX_INITIALIZED_GROUPS - 1], 0);
    ok_eq_ulonglong(Affinity.StaticBitmap[KAFFINITY_EX_INITIALIZED_GROUPS], (KAFFINITY)0xA5A5A5A5A5A5A5A5ULL);
    ok(KeIsEmptyAffinityEx(&Affinity), "fresh affinity is not empty\n");

    RtlInitUnicodeString(&Name, L"KeInitializeAffinityEx2");
    InitializeAffinityEx2 = (PKMT_KE_INITIALIZE_AFFINITY_EX2)MmGetSystemRoutineAddress(&Name);
    if (InitializeAffinityEx2 == NULL)
    {
        skip(FALSE, "KeInitializeAffinityEx2 is not exported\n");
        return;
    }

    for (Size = 0; Size <= KMT_AFFINITY_EX2_GROUPS; Size++)
    {
        RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), 0xA5);
        AffinityEx2Source = AffinityEx2Buffer;
        InitializeAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, Size);
        ok_eq_size(RtlCompareMemory(AffinityEx2Buffer.GuardBefore, AffinityEx2Source.GuardBefore, sizeof(AffinityEx2Buffer.GuardBefore)), sizeof(AffinityEx2Buffer.GuardBefore));
        ok_eq_uint(AffinityEx2Buffer.Affinity.Count, 1);
        ok_eq_uint(AffinityEx2Buffer.Affinity.Size, Size);
        ok_eq_ulong(AffinityEx2Buffer.Affinity.Reserved, 0);
        for (GroupNumber = 0; GroupNumber < KMT_AFFINITY_EX2_GROUPS; GroupNumber++)
            ok_eq_ulonglong(AffinityEx2Buffer.Affinity.Bitmap[GroupNumber], GroupNumber < Size ? 0 : AffinityEx2Source.Affinity.Bitmap[GroupNumber]);
        ok_eq_size(RtlCompareMemory(AffinityEx2Buffer.GuardAfter, AffinityEx2Source.GuardAfter, sizeof(AffinityEx2Buffer.GuardAfter)), sizeof(AffinityEx2Buffer.GuardAfter));
    }

    RtlInitUnicodeString(&Name, L"KeInitializeEnumerationContext");
    InitializeEnumerationContext = (PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT)MmGetSystemRoutineAddress(&Name);
    if (InitializeEnumerationContext == NULL)
    {
        skip(FALSE, "KeInitializeEnumerationContext is not exported\n");
        return;
    }

    for (Size = 0; Size <= KMT_AFFINITY_EX2_GROUPS; Size++)
    {
        for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
        {
            RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), 0x3C);
            AffinityEx2Buffer.Affinity.Count = Size;
            AffinityEx2Buffer.Affinity.Size = KMT_AFFINITY_EX2_GROUPS - Size;
            AffinityEx2Buffer.Affinity.Reserved = 0x5AA55AA5;
            AffinityEx2Buffer.Affinity.Bitmap[0] = EnumerationMasks[MaskIndex];
            AffinityEx2Source = AffinityEx2Buffer;
            RtlFillMemory(&EnumerationBuffer, sizeof(EnumerationBuffer), 0xA5);
            EnumerationSource = EnumerationBuffer;
            InitializeEnumerationContext(&EnumerationBuffer.Context, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardBefore, EnumerationSource.GuardBefore, sizeof(EnumerationBuffer.GuardBefore)), sizeof(EnumerationBuffer.GuardBefore));
            ok(EnumerationBuffer.Context.Affinity == (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, "context affinity %p, expected %p\n", EnumerationBuffer.Context.Affinity, &AffinityEx2Buffer.Affinity);
            ok_eq_ulonglong(EnumerationBuffer.Context.CurrentAffinity, EnumerationMasks[MaskIndex]);
            ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, 0);
            ok_eq_size(RtlCompareMemory((PUCHAR)&EnumerationBuffer.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationBuffer.Context.CurrentGroup), (PUCHAR)&EnumerationSource.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationSource.Context.CurrentGroup), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup)), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup));
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardAfter, EnumerationSource.GuardAfter, sizeof(EnumerationBuffer.GuardAfter)), sizeof(EnumerationBuffer.GuardAfter));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
        }
    }

    RtlInitUnicodeString(&Name, L"KeInitializeEnumerationContextFromAffinity");
    InitializeEnumerationContextFromAffinity = (PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT_FROM_AFFINITY)MmGetSystemRoutineAddress(&Name);
    if (InitializeEnumerationContextFromAffinity == NULL)
    {
        skip(FALSE, "KeInitializeEnumerationContextFromAffinity is not exported\n");
        return;
    }

    for (GroupValue = 0; GroupValue <= 0xFFFF; GroupValue++)
    {
        for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
        {
            RtlFillMemory(&EnumerationBuffer, sizeof(EnumerationBuffer), 0xA5);
            EnumerationSource = EnumerationBuffer;
            InitializeEnumerationContextFromAffinity(&EnumerationBuffer.Context, (USHORT)GroupValue, EnumerationMasks[MaskIndex]);
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardBefore, EnumerationSource.GuardBefore, sizeof(EnumerationBuffer.GuardBefore)), sizeof(EnumerationBuffer.GuardBefore));
            ok(EnumerationBuffer.Context.Affinity == NULL, "context affinity %p, expected NULL\n", EnumerationBuffer.Context.Affinity);
            ok_eq_ulonglong(EnumerationBuffer.Context.CurrentAffinity, EnumerationMasks[MaskIndex]);
            ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, GroupValue);
            ok_eq_size(RtlCompareMemory((PUCHAR)&EnumerationBuffer.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationBuffer.Context.CurrentGroup), (PUCHAR)&EnumerationSource.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationSource.Context.CurrentGroup), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup)), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup));
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardAfter, EnumerationSource.GuardAfter, sizeof(EnumerationBuffer.GuardAfter)), sizeof(EnumerationBuffer.GuardAfter));
        }
    }

    RtlInitUnicodeString(&Name, L"KeInitializeEnumerationContextFromGroup");
    InitializeEnumerationContextFromGroup = (PKMT_KE_INITIALIZE_ENUMERATION_CONTEXT_FROM_GROUP)MmGetSystemRoutineAddress(&Name);
    if (InitializeEnumerationContextFromGroup == NULL)
    {
        skip(FALSE, "KeInitializeEnumerationContextFromGroup is not exported\n");
        return;
    }

    for (GroupValue = 0; GroupValue <= 0xFFFF; GroupValue++)
    {
        for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
        {
            RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(GroupValue ^ MaskIndex));
            GroupBuffer.Affinity.Mask = EnumerationMasks[MaskIndex];
            GroupBuffer.Affinity.Group = (USHORT)GroupValue;
            GroupBufferSource = GroupBuffer;
            RtlFillMemory(&EnumerationBuffer, sizeof(EnumerationBuffer), 0xA5);
            EnumerationSource = EnumerationBuffer;
            InitializeEnumerationContextFromGroup(&EnumerationBuffer.Context, &GroupBuffer.Affinity);
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardBefore, EnumerationSource.GuardBefore, sizeof(EnumerationBuffer.GuardBefore)), sizeof(EnumerationBuffer.GuardBefore));
            ok(EnumerationBuffer.Context.Affinity == NULL, "context affinity %p, expected NULL\n", EnumerationBuffer.Context.Affinity);
            ok_eq_ulonglong(EnumerationBuffer.Context.CurrentAffinity, EnumerationMasks[MaskIndex]);
            ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, GroupValue);
            ok_eq_size(RtlCompareMemory((PUCHAR)&EnumerationBuffer.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationBuffer.Context.CurrentGroup), (PUCHAR)&EnumerationSource.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationSource.Context.CurrentGroup), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup)), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup));
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardAfter, EnumerationSource.GuardAfter, sizeof(EnumerationBuffer.GuardAfter)), sizeof(EnumerationBuffer.GuardAfter));
            ok_eq_size(RtlCompareMemory(&GroupBuffer, &GroupBufferSource, sizeof(GroupBuffer)), sizeof(GroupBuffer));
        }
    }

    KeAddGroupAffinityEx(&Affinity, 0, 3);
    ok_eq_ulonglong(KeQueryGroupAffinityEx(&Affinity, 0), 3);
    KeAddGroupAffinityEx(&Affinity, 0, 4);
    ok_eq_ulonglong(KeQueryGroupAffinityEx(&Affinity, 0), 7);
    KeRemoveGroupAffinityEx(&Affinity, 0, 2);
    ok_eq_ulonglong(KeQueryGroupAffinityEx(&Affinity, 0), 5);
    ok(!KeIsEmptyAffinityEx(&Affinity), "populated affinity is empty\n");

    KeRemoveProcessorAffinityEx(&Affinity, 0);
    ok(!KeCheckProcessorAffinityEx(&Affinity, 0), "processor 0 remained set\n");
    KeAddProcessorAffinityEx(&Affinity, 0);
    ok(KeCheckProcessorAffinityEx(&Affinity, 0), "processor 0 was not set\n");

    KeReinitializeAffinityEx(&Affinity);
    ok_eq_uint(Affinity.Count, 1);
    ok_eq_ulonglong(KeQueryGroupAffinityEx(&Affinity, 0), 0);
    ok(KeIsEmptyAffinityEx(&Affinity), "reinitialized affinity is not empty\n");

    RtlInitUnicodeString(&Name, L"KeSizeOfAffinityEx");
    SizeOfAffinityEx = (PKMT_KE_SIZE_OF_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (SizeOfAffinityEx == NULL)
    {
        skip(FALSE, "KeSizeOfAffinityEx is not exported\n");
        return;
    }

    ok_eq_size(SizeOfAffinityEx(0), (SIZE_T)8);
    ok_eq_size(SizeOfAffinityEx(1), (SIZE_T)16);
    ok_eq_size(SizeOfAffinityEx(32), (SIZE_T)264);
    ok_eq_size(SizeOfAffinityEx(MAXUSHORT), (SIZE_T)524288);

    RtlInitUnicodeString(&Name, L"KeCountSetBitsAffinityEx");
    CountSetBitsAffinityEx = (PKMT_KE_COUNT_SET_BITS_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (CountSetBitsAffinityEx == NULL)
    {
        skip(FALSE, "KeCountSetBitsAffinityEx is not exported\n");
        return;
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Bitmap[0] = ~(KAFFINITY)0;
    ok_eq_ulong(CountSetBitsAffinityEx(&Affinity), 0);
    Affinity.Count = 1;
    Affinity.Bitmap[0] = ((KAFFINITY)1 << 63) | 1;
    ok_eq_ulong(CountSetBitsAffinityEx(&Affinity), 2);
    Affinity.Count = 3;
    Affinity.Bitmap[1] = ~(KAFFINITY)0;
    Affinity.Bitmap[2] = 0xE0;
    ok_eq_ulong(CountSetBitsAffinityEx(&Affinity), 69);

    RtlInitUnicodeString(&Name, L"KeCopyAffinityEx");
    CopyAffinityEx = (PKMT_KE_COPY_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (CopyAffinityEx == NULL)
    {
        skip(FALSE, "KeCopyAffinityEx is not exported\n");
        return;
    }

    RtlFillMemory(&Affinity, sizeof(Affinity), 0xA5);
    RtlZeroMemory(&Source, sizeof(Source));
    Source.Count = 3;
    Source.Size = 1;
    Source.Reserved = MAXULONG;
    Source.Bitmap[0] = 1;
    Source.Bitmap[1] = 2;
    Source.Bitmap[2] = 4;
    CopyAffinityEx(&Affinity, &Source);
    ok_eq_uint(Affinity.Count, 3);
    ok_eq_uint(Affinity.Size, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulong(Affinity.Reserved, 0);
    ok_eq_ulonglong(Affinity.Bitmap[0], 1);
    ok_eq_ulonglong(Affinity.Bitmap[2], 4);
    ok_eq_ulonglong(Affinity.Bitmap[3], 0);
    ok_eq_ulonglong(Affinity.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS - 1], 0);
    ok_eq_ulonglong(Affinity.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS], (KAFFINITY)0xA5A5A5A5A5A5A5A5ULL);

    RtlFillMemory(&Affinity, sizeof(Affinity), 0xA5);
    RtlZeroMemory(&Source, sizeof(Source));
    Source.Count = KAFFINITY_EX_INITIALIZED_GROUPS + 1;
    Source.Size = KAFFINITY_EX_STATIC_GROUPS;
    Source.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS - 1] = 0x19;
    Source.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS] = 0x20;
    CopyAffinityEx(&Affinity, &Source);
    ok_eq_uint(Affinity.Count, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulonglong(Affinity.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS - 1], 0x19);
    ok_eq_ulonglong(Affinity.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS], (KAFFINITY)0xA5A5A5A5A5A5A5A5ULL);

    RtlInitUnicodeString(&Name, L"KeIsEqualAffinityEx");
    IsEqualAffinityEx = (PKMT_KE_IS_EQUAL_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (IsEqualAffinityEx == NULL)
    {
        skip(FALSE, "KeIsEqualAffinityEx is not exported\n");
        return;
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    RtlZeroMemory(&Source, sizeof(Source));
    Affinity.Size = 1;
    Affinity.Reserved = MAXULONG;
    Source.Count = 1;
    Source.Size = KAFFINITY_EX_STATIC_GROUPS;
    ok(IsEqualAffinityEx(&Affinity, &Source), "zero trailing group changed equality\n");
    Source.Bitmap[0] = 1;
    ok(!IsEqualAffinityEx(&Affinity, &Source), "nonzero trailing group compared equal\n");
    Affinity.Count = 1;
    Affinity.Bitmap[0] = 1;
    ok(IsEqualAffinityEx(&Affinity, &Source), "equal one-group affinities differed\n");
    Source.Count = 3;
    ok(IsEqualAffinityEx(&Affinity, &Source), "zero extended groups changed equality\n");
    ok(IsEqualAffinityEx(&Source, &Affinity), "equality depended on operand order\n");
    Source.Bitmap[2] = (KAFFINITY)1 << 63;
    ok(!IsEqualAffinityEx(&Affinity, &Source), "nonzero extended group compared equal\n");
    Source.Bitmap[2] = 0;
    Source.Bitmap[0] = 2;
    ok(!IsEqualAffinityEx(&Affinity, &Source), "different common group compared equal\n");

    RtlInitUnicodeString(&Name, L"KeIsSubsetAffinityEx");
    IsSubsetAffinityEx = (PKMT_KE_IS_SUBSET_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (IsSubsetAffinityEx == NULL)
    {
        skip(FALSE, "KeIsSubsetAffinityEx is not exported\n");
        return;
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    RtlZeroMemory(&Source, sizeof(Source));
    Affinity.Size = MAXUSHORT;
    Affinity.Reserved = MAXULONG;
    Source.Count = 1;
    Source.Size = 1;
    Source.Bitmap[0] = 1;
    ok(IsSubsetAffinityEx(&Affinity, &Source), "empty affinity was not a subset\n");
    ok(!IsSubsetAffinityEx(&Source, &Affinity), "nonempty trailing group was accepted as a subset\n");
    Affinity.Count = 1;
    Affinity.Bitmap[0] = 5;
    Source.Bitmap[0] = 7;
    ok(IsSubsetAffinityEx(&Affinity, &Source), "common-group subset was rejected\n");
    ok(!IsSubsetAffinityEx(&Source, &Affinity), "common-group non-subset was accepted\n");
    Source.Count = 3;
    Source.Bitmap[2] = (KAFFINITY)1 << 63;
    ok(IsSubsetAffinityEx(&Affinity, &Source), "superset-only trailing groups changed the result\n");
    Affinity.Count = 3;
    ok(IsSubsetAffinityEx(&Affinity, &Source), "zero subset trailing groups changed the result\n");
    Affinity.Bitmap[1] = 1;
    ok(!IsSubsetAffinityEx(&Affinity, &Source), "nonzero subset trailing group was accepted\n");
    Affinity.Bitmap[1] = 0;
    Affinity.Bitmap[0] = 8;
    ok(!IsSubsetAffinityEx(&Affinity, &Source), "bit outside the common-group superset was accepted\n");

    RtlInitUnicodeString(&Name, L"KeIsSingleGroupAffinityEx");
    IsSingleGroupAffinityEx = (PKMT_KE_IS_SINGLE_GROUP_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (IsSingleGroupAffinityEx == NULL)
    {
        skip(FALSE, "KeIsSingleGroupAffinityEx is not exported\n");
        return;
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Size = MAXUSHORT;
    Affinity.Reserved = MAXULONG;
    GroupNumber = MAXUSHORT;
    ok(!IsSingleGroupAffinityEx(&Affinity, &GroupNumber), "empty affinity reported one group\n");
    ok_eq_uint(GroupNumber, KAFFINITY_EX_STATIC_GROUPS);
    ok(!IsSingleGroupAffinityEx(&Affinity, NULL), "empty affinity with no output reported one group\n");
    Affinity.Count = 3;
    Affinity.Bitmap[2] = 0x80;
    ok(IsSingleGroupAffinityEx(&Affinity, &GroupNumber), "single nonzero group was rejected\n");
    ok_eq_uint(GroupNumber, 2);
    ok(IsSingleGroupAffinityEx(&Affinity, NULL), "optional output changed the single-group result\n");
    Affinity.Bitmap[0] = 1;
    GroupNumber = MAXUSHORT;
    ok(!IsSingleGroupAffinityEx(&Affinity, &GroupNumber), "multiple nonzero groups were accepted\n");
    ok_eq_uint(GroupNumber, 0);
    RtlZeroMemory(Affinity.Bitmap, sizeof(Affinity.StaticBitmap));
    Affinity.Count = KAFFINITY_EX_STATIC_GROUPS;
    Affinity.Bitmap[KAFFINITY_EX_STATIC_GROUPS - 1] = 1;
    ok(IsSingleGroupAffinityEx(&Affinity, &GroupNumber), "last supported group was rejected\n");
    ok_eq_uint(GroupNumber, KAFFINITY_EX_STATIC_GROUPS - 1);

    RtlInitUnicodeString(&Name, L"KeAndAffinityEx");
    AndAffinityEx = (PKMT_KE_AND_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (AndAffinityEx == NULL)
    {
        skip(FALSE, "KeAndAffinityEx is not exported\n");
        return;
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    RtlZeroMemory(&Source, sizeof(Source));
    Affinity.Count = 3;
    Affinity.Size = MAXUSHORT;
    Affinity.Reserved = MAXULONG;
    Affinity.Bitmap[0] = 5;
    Affinity.Bitmap[1] = 8;
    Affinity.Bitmap[2] = 0x80;
    Source.Count = 2;
    Source.Size = 1;
    Source.Bitmap[0] = 2;
    Source.Bitmap[1] = 4;
    ok(!AndAffinityEx(&Affinity, &Source, NULL), "disjoint affinities intersected\n");
    Source.Bitmap[0] = 7;
    RtlFillMemory(&Result, sizeof(Result), 0xA5);
    ok(AndAffinityEx(&Affinity, &Source, &Result), "nonempty intersection was rejected\n");
    ok_eq_uint(Result.Count, 2);
    ok_eq_uint(Result.Size, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulong(Result.Reserved, 0);
    ok_eq_ulonglong(Result.Bitmap[0], 5);
    ok_eq_ulonglong(Result.Bitmap[1], 0);
    ok_eq_ulonglong(Result.Bitmap[2], 0);
    ok_eq_ulonglong(Result.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS - 1], 0);
    ok_eq_ulonglong(Result.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS], (KAFFINITY)0xA5A5A5A5A5A5A5A5ULL);

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    RtlZeroMemory(&Source, sizeof(Source));
    Affinity.Count = 2;
    Affinity.Bitmap[0] = 5;
    Affinity.Bitmap[1] = 8;
    Source.Count = 1;
    Source.Bitmap[0] = 3;
    ok(AndAffinityEx(&Affinity, &Source, &Affinity), "in-place intersection was rejected\n");
    ok_eq_uint(Affinity.Count, 1);
    ok_eq_uint(Affinity.Size, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulonglong(Affinity.Bitmap[0], 1);
    ok_eq_ulonglong(Affinity.Bitmap[1], 0);

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    RtlZeroMemory(&Source, sizeof(Source));
    Affinity.Count = KAFFINITY_EX_INITIALIZED_GROUPS + 1;
    Source.Count = KAFFINITY_EX_INITIALIZED_GROUPS + 1;
    Affinity.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS] = 1;
    Source.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS] = 1;
    ok(AndAffinityEx(&Affinity, &Source, NULL), "unbuffered intersection ignored an extended group\n");
    RtlFillMemory(&Result, sizeof(Result), 0xA5);
    ok(!AndAffinityEx(&Affinity, &Source, &Result), "buffered intersection scanned past its fixed capacity\n");
    ok_eq_uint(Result.Count, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_uint(Result.Size, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulonglong(Result.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS - 1], 0);
    ok_eq_ulonglong(Result.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS], (KAFFINITY)0xA5A5A5A5A5A5A5A5ULL);

    RtlInitUnicodeString(&Name, L"KeAndAffinityEx2");
    AndAffinityEx2 = (PKMT_KE_AND_AFFINITY_EX2)MmGetSystemRoutineAddress(&Name);
    if (AndAffinityEx2 == NULL)
    {
        skip(FALSE, "KeAndAffinityEx2 is not exported\n");
        return;
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(AndAffinityEx2CountPairs); CountIndex++)
    {
        AffinityCount1 = AndAffinityEx2CountPairs[CountIndex][0];
        AffinityCount2 = AndAffinityEx2CountPairs[CountIndex][1];
        for (SizeIndex = 0; SizeIndex < RTL_NUMBER_OF(AffinityEx2Sizes); SizeIndex++)
        {
            ResultSize = AffinityEx2Sizes[SizeIndex];
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ SizeIndex ^ PatternIndex));
                RtlFillMemory(&AffinityEx2Buffer2, sizeof(AffinityEx2Buffer2), (UCHAR)(0x3C ^ CountIndex ^ SizeIndex ^ PatternIndex));
                AffinityEx2Buffer.Affinity.Count = AffinityCount1;
                AffinityEx2Buffer.Affinity.Size = ResultSize;
                AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
                AffinityEx2Buffer2.Affinity.Count = AffinityCount2;
                AffinityEx2Buffer2.Affinity.Size = AffinityEx2Sizes[(CountIndex + SizeIndex + 1) % RTL_NUMBER_OF(AffinityEx2Sizes)];
                AffinityEx2Buffer2.Affinity.Reserved = 0x5A6B7C8DUL ^ PatternIndex;
                for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
                {
                    Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U + SizeIndex * 0x55U);
                    Combination = (KAFFINITY)Pattern;
                    Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                    Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                    Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                    AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
                    if ((PatternIndex & 3) == 0)
                        AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = ~Combination;
                    else if ((PatternIndex & 3) == 1)
                        AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = Combination;
                    else if ((PatternIndex & 3) == 2)
                        AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = Combination ^ (KAFFINITY)0xD1B54A32D192ED03ULL;
                    else
                        AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = (KAFFINITY)1 << ((PatternIndex + GroupValue) % (sizeof(KAFFINITY) * 8));
                }
                AffinityEx2Source = AffinityEx2Buffer;
                AffinityEx2Source2 = AffinityEx2Buffer2;

                RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ SizeIndex ^ PatternIndex));
                AffinityEx2Result.Affinity.Size = ResultSize;
                AffinityEx2ResultExpected = AffinityEx2Result;
                CommonCount = min(AffinityCount1, AffinityCount2);
                ResultCount = min(CommonCount, ResultSize);
                AffinityEx2ResultExpected.Affinity.Count = ResultCount;
                AffinityEx2ResultExpected.Affinity.Size = ResultSize;
                AffinityEx2ResultExpected.Affinity.Reserved = 0;
                ExpectedLogical = FALSE;
                for (GroupValue = 0; GroupValue < ResultSize; GroupValue++)
                {
                    ExpectedMask = GroupValue < ResultCount ? AffinityEx2Source.Affinity.Bitmap[GroupValue] & AffinityEx2Source2.Affinity.Bitmap[GroupValue] : 0;
                    AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ExpectedMask;
                    if (ExpectedMask != 0)
                        ExpectedLogical = TRUE;
                }
                LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Result.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));

                AffinityEx2AliasExpected = AffinityEx2Source;
                AliasSize = AffinityEx2Source.Affinity.Size;
                AliasCount = min(CommonCount, AliasSize);
                AffinityEx2AliasExpected.Affinity.Count = AliasCount;
                AffinityEx2AliasExpected.Affinity.Size = AliasSize;
                AffinityEx2AliasExpected.Affinity.Reserved = 0;
                ExpectedLogical = FALSE;
                for (GroupValue = 0; GroupValue < AliasSize; GroupValue++)
                {
                    ExpectedMask = GroupValue < AliasCount ? AffinityEx2Source.Affinity.Bitmap[GroupValue] & AffinityEx2Source2.Affinity.Bitmap[GroupValue] : 0;
                    AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = ExpectedMask;
                    if (ExpectedMask != 0)
                        ExpectedLogical = TRUE;
                }
                LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));

                AffinityEx2Buffer = AffinityEx2Source;
                AffinityEx2Buffer2 = AffinityEx2Source2;
                AffinityEx2AliasExpected = AffinityEx2Source2;
                AliasSize = AffinityEx2Source2.Affinity.Size;
                AliasCount = min(CommonCount, AliasSize);
                AffinityEx2AliasExpected.Affinity.Count = AliasCount;
                AffinityEx2AliasExpected.Affinity.Size = AliasSize;
                AffinityEx2AliasExpected.Affinity.Reserved = 0;
                ExpectedLogical = FALSE;
                for (GroupValue = 0; GroupValue < AliasSize; GroupValue++)
                {
                    ExpectedMask = GroupValue < AliasCount ? AffinityEx2Source.Affinity.Bitmap[GroupValue] & AffinityEx2Source2.Affinity.Bitmap[GroupValue] : 0;
                    AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = ExpectedMask;
                    if (ExpectedMask != 0)
                        ExpectedLogical = TRUE;
                }
                LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));

                AffinityEx2Buffer = AffinityEx2Source;
                AffinityEx2AliasExpected = AffinityEx2Source;
                AliasSize = AffinityEx2Source.Affinity.Size;
                AliasCount = min(AffinityCount1, AliasSize);
                AffinityEx2AliasExpected.Affinity.Count = AliasCount;
                AffinityEx2AliasExpected.Affinity.Size = AliasSize;
                AffinityEx2AliasExpected.Affinity.Reserved = 0;
                ExpectedLogical = FALSE;
                for (GroupValue = 0; GroupValue < AliasSize; GroupValue++)
                {
                    ExpectedMask = GroupValue < AliasCount ? AffinityEx2Source.Affinity.Bitmap[GroupValue] : 0;
                    AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = ExpectedMask;
                    if (ExpectedMask != 0)
                        ExpectedLogical = TRUE;
                }
                LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
            }
        }
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(AndAffinityEx2ExhaustiveTriples); CountIndex++)
    {
        AffinityCount1 = AndAffinityEx2ExhaustiveTriples[CountIndex][0];
        AffinityCount2 = AndAffinityEx2ExhaustiveTriples[CountIndex][1];
        ResultSize = AndAffinityEx2ExhaustiveTriples[CountIndex][2];
        for (PatternIndex = 0; PatternIndex <= MAXUSHORT; PatternIndex++)
        {
            RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ PatternIndex));
            RtlFillMemory(&AffinityEx2Buffer2, sizeof(AffinityEx2Buffer2), (UCHAR)(0x3C ^ CountIndex ^ PatternIndex));
            AffinityEx2Buffer.Affinity.Count = AffinityCount1;
            AffinityEx2Buffer.Affinity.Size = (USHORT)((PatternIndex + CountIndex) % (KMT_AFFINITY_EX2_GROUPS + 1));
            AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
            AffinityEx2Buffer2.Affinity.Count = AffinityCount2;
            AffinityEx2Buffer2.Affinity.Size = (USHORT)((PatternIndex + CountIndex + 1) % (KMT_AFFINITY_EX2_GROUPS + 1));
            AffinityEx2Buffer2.Affinity.Reserved = 0x5A6B7C8DUL ^ PatternIndex;
            for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
            {
                Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U);
                Combination = (KAFFINITY)Pattern;
                Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
                if ((PatternIndex & 3) == 0)
                    AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = ~Combination;
                else if ((PatternIndex & 3) == 1)
                    AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = Combination;
                else if ((PatternIndex & 3) == 2)
                    AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = Combination ^ (KAFFINITY)0xD1B54A32D192ED03ULL;
                else
                    AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = (KAFFINITY)1 << ((PatternIndex + GroupValue) % (sizeof(KAFFINITY) * 8));
            }
            AffinityEx2Source = AffinityEx2Buffer;
            AffinityEx2Source2 = AffinityEx2Buffer2;
            RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ PatternIndex));
            AffinityEx2Result.Affinity.Size = ResultSize;
            AffinityEx2ResultExpected = AffinityEx2Result;
            CommonCount = min(AffinityCount1, AffinityCount2);
            ResultCount = min(CommonCount, ResultSize);
            AffinityEx2ResultExpected.Affinity.Count = ResultCount;
            AffinityEx2ResultExpected.Affinity.Size = ResultSize;
            AffinityEx2ResultExpected.Affinity.Reserved = 0;
            ExpectedLogical = FALSE;
            for (GroupValue = 0; GroupValue < ResultSize; GroupValue++)
            {
                ExpectedMask = GroupValue < ResultCount ? AffinityEx2Source.Affinity.Bitmap[GroupValue] & AffinityEx2Source2.Affinity.Bitmap[GroupValue] : 0;
                AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ExpectedMask;
                if (ExpectedMask != 0)
                    ExpectedLogical = TRUE;
            }
            LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Result.Affinity);
            ok_eq_ulong(LogicalResult, ExpectedLogical);
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));
        }
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(AffinityEx2Sizes); CountIndex++)
    {
        AffinityCount1 = AffinityEx2Sizes[CountIndex];
        for (CountIndex2 = 0; CountIndex2 < RTL_NUMBER_OF(AffinityEx2Sizes); CountIndex2++)
        {
            AffinityCount2 = AffinityEx2Sizes[CountIndex2];
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ CountIndex2 ^ PatternIndex));
                RtlFillMemory(&AffinityEx2Buffer2, sizeof(AffinityEx2Buffer2), (UCHAR)(0x3C ^ CountIndex ^ CountIndex2 ^ PatternIndex));
                AffinityEx2Buffer.Affinity.Count = AffinityCount1;
                AffinityEx2Buffer.Affinity.Size = AffinityEx2Sizes[(CountIndex2 + 1) % RTL_NUMBER_OF(AffinityEx2Sizes)];
                AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
                AffinityEx2Buffer2.Affinity.Count = AffinityCount2;
                AffinityEx2Buffer2.Affinity.Size = AffinityEx2Sizes[(CountIndex + 1) % RTL_NUMBER_OF(AffinityEx2Sizes)];
                AffinityEx2Buffer2.Affinity.Reserved = 0x5A6B7C8DUL ^ PatternIndex;
                for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
                {
                    Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U + CountIndex2 * 0x55U);
                    Combination = (KAFFINITY)Pattern;
                    Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                    Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                    Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                    AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
                    if ((PatternIndex & 3) == 0)
                        AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = ~Combination;
                    else if ((PatternIndex & 3) == 1)
                        AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = Combination;
                    else if ((PatternIndex & 3) == 2)
                        AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = Combination ^ (KAFFINITY)0xD1B54A32D192ED03ULL;
                    else
                        AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = (KAFFINITY)1 << ((PatternIndex + GroupValue) % (sizeof(KAFFINITY) * 8));
                }
                AffinityEx2Source = AffinityEx2Buffer;
                AffinityEx2Source2 = AffinityEx2Buffer2;
                CommonCount = min(AffinityCount1, AffinityCount2);
                ExpectedLogical = FALSE;
                for (GroupValue = 0; GroupValue < CommonCount; GroupValue++)
                {
                    if ((AffinityEx2Source.Affinity.Bitmap[GroupValue] & AffinityEx2Source2.Affinity.Bitmap[GroupValue]) != 0)
                        ExpectedLogical = TRUE;
                }
                LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, NULL);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));
            }
        }
    }

    RtlZeroMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer));
    RtlZeroMemory(&AffinityEx2Buffer2, sizeof(AffinityEx2Buffer2));
    AffinityEx2Buffer.Affinity.Count = KMT_AFFINITY_EX2_GROUPS;
    AffinityEx2Buffer.Affinity.Size = 1;
    AffinityEx2Buffer.Affinity.Reserved = MAXULONG;
    AffinityEx2Buffer2.Affinity.Count = KMT_AFFINITY_EX2_GROUPS;
    AffinityEx2Buffer2.Affinity.Size = MAXUSHORT;
    AffinityEx2Buffer2.Affinity.Reserved = MAXULONG;
    AffinityEx2Buffer.Affinity.Bitmap[KMT_AFFINITY_EX2_GROUPS - 1] = (KAFFINITY)0x8000000000000001ULL;
    AffinityEx2Buffer2.Affinity.Bitmap[KMT_AFFINITY_EX2_GROUPS - 1] = (KAFFINITY)0x8000000000000001ULL;
    AffinityEx2Source = AffinityEx2Buffer;
    AffinityEx2Source2 = AffinityEx2Buffer2;
    LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, NULL);
    ok_eq_ulong(LogicalResult, TRUE);
    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));

    RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), 0xA5);
    AffinityEx2Result.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
    AffinityEx2ResultExpected = AffinityEx2Result;
    AffinityEx2ResultExpected.Affinity.Count = KAFFINITY_EX_INITIALIZED_GROUPS;
    AffinityEx2ResultExpected.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
    AffinityEx2ResultExpected.Affinity.Reserved = 0;
    RtlZeroMemory(AffinityEx2ResultExpected.Affinity.Bitmap, KAFFINITY_EX_INITIALIZED_GROUPS * sizeof(KAFFINITY));
    LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Result.Affinity);
    ok_eq_ulong(LogicalResult, FALSE);
    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));
    ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));

    RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), 0xA5);
    AffinityEx2Result.Affinity.Size = KMT_AFFINITY_EX2_GROUPS;
    AffinityEx2ResultExpected = AffinityEx2Result;
    AffinityEx2ResultExpected.Affinity.Count = KMT_AFFINITY_EX2_GROUPS;
    AffinityEx2ResultExpected.Affinity.Size = KMT_AFFINITY_EX2_GROUPS;
    AffinityEx2ResultExpected.Affinity.Reserved = 0;
    RtlZeroMemory(AffinityEx2ResultExpected.Affinity.Bitmap, KMT_AFFINITY_EX2_GROUPS * sizeof(KAFFINITY));
    AffinityEx2ResultExpected.Affinity.Bitmap[KMT_AFFINITY_EX2_GROUPS - 1] = (KAFFINITY)0x8000000000000001ULL;
    LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Result.Affinity);
    ok_eq_ulong(LogicalResult, TRUE);
    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));
    ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));

    AffinityEx2Buffer2.Affinity.Count = KMT_AFFINITY_EX2_GROUPS - 1;
    AffinityEx2Source2 = AffinityEx2Buffer2;
    LogicalResult = AndAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, NULL);
    ok_eq_ulong(LogicalResult, FALSE);
    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));

    RtlInitUnicodeString(&Name, L"KeOrAffinityEx");
    OrAffinityEx = (PKMT_KE_OR_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (OrAffinityEx == NULL)
    {
        skip(FALSE, "KeOrAffinityEx is not exported\n");
        return;
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    RtlZeroMemory(&Source, sizeof(Source));
    Affinity.Count = 3;
    Affinity.Size = MAXUSHORT;
    Affinity.Reserved = MAXULONG;
    Affinity.Bitmap[0] = 1;
    Affinity.Bitmap[2] = 8;
    Source.Count = 2;
    Source.Size = 1;
    Source.Bitmap[0] = 2;
    Source.Bitmap[1] = 4;
    RtlFillMemory(&Result, sizeof(Result), 0xA5);
    ok(OrAffinityEx(&Affinity, &Source, &Result), "nonempty union was rejected\n");
    ok_eq_uint(Result.Count, 3);
    ok_eq_uint(Result.Size, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulong(Result.Reserved, 0);
    ok_eq_ulonglong(Result.Bitmap[0], 3);
    ok_eq_ulonglong(Result.Bitmap[1], 4);
    ok_eq_ulonglong(Result.Bitmap[2], 8);
    ok_eq_ulonglong(Result.Bitmap[3], 0);
    ok_eq_ulonglong(Result.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS - 1], 0);
    ok_eq_ulonglong(Result.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS], (KAFFINITY)0xA5A5A5A5A5A5A5A5ULL);

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    RtlZeroMemory(&Source, sizeof(Source));
    Affinity.Count = 1;
    Affinity.Bitmap[0] = 1;
    Source.Count = 2;
    Source.Bitmap[0] = 2;
    Source.Bitmap[1] = 4;
    ok(OrAffinityEx(&Affinity, &Source, &Affinity), "in-place union was rejected\n");
    ok_eq_uint(Affinity.Count, 2);
    ok_eq_uint(Affinity.Size, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulonglong(Affinity.Bitmap[0], 3);
    ok_eq_ulonglong(Affinity.Bitmap[1], 4);
    ok_eq_ulonglong(Affinity.Bitmap[2], 0);

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    RtlZeroMemory(&Source, sizeof(Source));
    Affinity.Count = KAFFINITY_EX_INITIALIZED_GROUPS + 1;
    Source.Count = KAFFINITY_EX_INITIALIZED_GROUPS + 1;
    ok(!OrAffinityEx(&Affinity, &Source, NULL), "empty unbuffered union was nonempty\n");
    Affinity.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS] = 1;
    ok(OrAffinityEx(&Affinity, &Source, NULL), "unbuffered union ignored an extended group\n");
    RtlFillMemory(&Result, sizeof(Result), 0xA5);
    ok(!OrAffinityEx(&Affinity, &Source, &Result), "buffered union scanned past its fixed capacity\n");
    ok_eq_uint(Result.Count, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_uint(Result.Size, KAFFINITY_EX_INITIALIZED_GROUPS);
    ok_eq_ulonglong(Result.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS - 1], 0);
    ok_eq_ulonglong(Result.Bitmap[KAFFINITY_EX_INITIALIZED_GROUPS], (KAFFINITY)0xA5A5A5A5A5A5A5A5ULL);

    RtlInitUnicodeString(&Name, L"KeOrAffinityEx2");
    OrAffinityEx2 = (PKMT_KE_OR_AFFINITY_EX2)MmGetSystemRoutineAddress(&Name);
    if (OrAffinityEx2 == NULL)
    {
        skip(FALSE, "KeOrAffinityEx2 is not exported\n");
        return;
    }
    KmtTestOrAffinityEx2(OrAffinityEx2);

    RtlInitUnicodeString(&Name, L"KeGetProcessorIndexFromNumber");
    GetProcessorIndexFromNumber = (PKMT_KE_GET_PROCESSOR_INDEX_FROM_NUMBER)MmGetSystemRoutineAddress(&Name);
    if (GetProcessorIndexFromNumber == NULL)
    {
        skip(FALSE, "KeGetProcessorIndexFromNumber is not exported\n");
        return;
    }

    ActiveCount = KeQueryActiveProcessorCount(NULL);
    ok((ActiveCount > 1) && (ActiveCount < 64), "unexpected active processor count %lu\n", ActiveCount);
    RtlZeroMemory(&ProcessorNumber, sizeof(ProcessorNumber));
    ok_eq_ulong(GetProcessorIndexFromNumber(&ProcessorNumber), 0);
    ProcessorNumber.Reserved = 1;
    ok_eq_ulong(GetProcessorIndexFromNumber(&ProcessorNumber), INVALID_PROCESSOR_INDEX);
    ProcessorNumber.Reserved = 0;
    ProcessorNumber.Group = 1;
    ok_eq_ulong(GetProcessorIndexFromNumber(&ProcessorNumber), INVALID_PROCESSOR_INDEX);
    ProcessorNumber.Group = 0;
    ProcessorNumber.Number = 64;
    ok_eq_ulong(GetProcessorIndexFromNumber(&ProcessorNumber), INVALID_PROCESSOR_INDEX);
    ProcessorNumber.Number = 1;
    ok_eq_ulong(GetProcessorIndexFromNumber(&ProcessorNumber), 1);
    ProcessorNumber.Number = (UCHAR)(ActiveCount - 1);
    ok_eq_ulong(GetProcessorIndexFromNumber(&ProcessorNumber), ActiveCount - 1);
    ProcessorNumber.Number = (UCHAR)ActiveCount;
    ok_eq_ulong(GetProcessorIndexFromNumber(&ProcessorNumber), INVALID_PROCESSOR_INDEX);

    RtlInitUnicodeString(&Name, L"KeGetProcessorNumberFromIndex");
    GetProcessorNumberFromIndex = (PKMT_KE_GET_PROCESSOR_NUMBER_FROM_INDEX)MmGetSystemRoutineAddress(&Name);
    if (GetProcessorNumberFromIndex == NULL)
    {
        skip(FALSE, "KeGetProcessorNumberFromIndex is not exported\n");
        return;
    }

    RtlFillMemory(&ProcessorNumber, sizeof(ProcessorNumber), 0xA5);
    ok_eq_hex(GetProcessorNumberFromIndex(0, &ProcessorNumber), STATUS_SUCCESS);
    ok_eq_uint(ProcessorNumber.Group, 0);
    ok_eq_uint(ProcessorNumber.Number, 0);
    ok_eq_uint(ProcessorNumber.Reserved, 0);
    RtlFillMemory(&ProcessorNumber, sizeof(ProcessorNumber), 0xA5);
    ok_eq_hex(GetProcessorNumberFromIndex(1, &ProcessorNumber), STATUS_SUCCESS);
    ok_eq_uint(ProcessorNumber.Group, 0);
    ok_eq_uint(ProcessorNumber.Number, 1);
    ok_eq_uint(ProcessorNumber.Reserved, 0);
    RtlFillMemory(&ProcessorNumber, sizeof(ProcessorNumber), 0xA5);
    ok_eq_hex(GetProcessorNumberFromIndex(ActiveCount - 1, &ProcessorNumber), STATUS_SUCCESS);
    ok_eq_uint(ProcessorNumber.Number, ActiveCount - 1);
    RtlFillMemory(&ProcessorNumber, sizeof(ProcessorNumber), 0xA5);
    ok_eq_hex(GetProcessorNumberFromIndex(ActiveCount, &ProcessorNumber), STATUS_INVALID_PARAMETER);
    ok_eq_uint(ProcessorNumber.Group, 0xA5A5);
    ok_eq_uint(ProcessorNumber.Number, 0xA5);
    ok_eq_uint(ProcessorNumber.Reserved, 0xA5);
    ok_eq_hex(GetProcessorNumberFromIndex(MAXULONG, &ProcessorNumber), STATUS_INVALID_PARAMETER);
    ok_eq_uint(ProcessorNumber.Group, 0xA5A5);

    RtlInitUnicodeString(&Name, L"KeProcessorGroupAffinity");
    ProcessorGroupAffinity = (PKMT_KE_PROCESSOR_GROUP_AFFINITY)MmGetSystemRoutineAddress(&Name);
    if (ProcessorGroupAffinity == NULL)
    {
        skip(FALSE, "KeProcessorGroupAffinity is not exported\n");
        return;
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
        RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(0xA5 ^ ProcessorIndex));
        GroupBufferSource = GroupBuffer;
        ProcessorGroupAffinity(&GroupBuffer.Affinity, ProcessorIndex);
        RtlZeroMemory(&GroupAffinity, sizeof(GroupAffinity));
        GroupAffinity.Mask = (KAFFINITY)1 << ProcessorNumber.Number;
        GroupAffinity.Group = ProcessorNumber.Group;
        ok_eq_size(RtlCompareMemory(GroupBuffer.GuardBefore, GroupBufferSource.GuardBefore, sizeof(GroupBuffer.GuardBefore)), sizeof(GroupBuffer.GuardBefore));
        ok_eq_size(RtlCompareMemory(&GroupBuffer.Affinity, &GroupAffinity, sizeof(GroupAffinity)), sizeof(GroupAffinity));
        ok_eq_size(RtlCompareMemory(GroupBuffer.GuardAfter, GroupBufferSource.GuardAfter, sizeof(GroupBuffer.GuardAfter)), sizeof(GroupBuffer.GuardAfter));
    }

    RtlInitUnicodeString(&Name, L"KeAddProcessorGroupAffinity");
    AddProcessorGroupAffinity = (PKMT_KE_ADD_PROCESSOR_GROUP_AFFINITY)MmGetSystemRoutineAddress(&Name);
    if (AddProcessorGroupAffinity == NULL)
    {
        skip(FALSE, "KeAddProcessorGroupAffinity is not exported\n");
        return;
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
        for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
        {
            RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(ProcessorIndex ^ MaskIndex));
            GroupBuffer.Affinity.Mask = EnumerationMasks[MaskIndex];
            GroupBufferSource = GroupBuffer;
            GroupAffinity = GroupBuffer.Affinity;
            GroupAffinity.Mask |= (KAFFINITY)1 << ProcessorNumber.Number;
            AddProcessorGroupAffinity(&GroupBuffer.Affinity, ProcessorIndex);
            ok_eq_size(RtlCompareMemory(GroupBuffer.GuardBefore, GroupBufferSource.GuardBefore, sizeof(GroupBuffer.GuardBefore)), sizeof(GroupBuffer.GuardBefore));
            ok_eq_size(RtlCompareMemory(&GroupBuffer.Affinity, &GroupAffinity, sizeof(GroupAffinity)), sizeof(GroupAffinity));
            ok_eq_size(RtlCompareMemory(GroupBuffer.GuardAfter, GroupBufferSource.GuardAfter, sizeof(GroupBuffer.GuardAfter)), sizeof(GroupBuffer.GuardAfter));
        }
    }

    RtlInitUnicodeString(&Name, L"KeCheckProcessorGroupAffinity");
    CheckProcessorGroupAffinity = (PKMT_KE_CHECK_PROCESSOR_GROUP_AFFINITY)MmGetSystemRoutineAddress(&Name);
    if (CheckProcessorGroupAffinity == NULL)
    {
        skip(FALSE, "KeCheckProcessorGroupAffinity is not exported\n");
        return;
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
        for (GroupValue = 0; GroupValue <= MAXUSHORT; GroupValue++)
        {
            for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
            {
                RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(ProcessorIndex ^ GroupValue ^ MaskIndex));
                GroupBuffer.Affinity.Mask = EnumerationMasks[MaskIndex];
                GroupBuffer.Affinity.Group = (USHORT)GroupValue;
                GroupBufferSource = GroupBuffer;
                ExpectedLogical = ((USHORT)GroupValue == ProcessorNumber.Group) && ((EnumerationMasks[MaskIndex] & ((KAFFINITY)1 << ProcessorNumber.Number)) != 0);
                LogicalResult = CheckProcessorGroupAffinity(&GroupBuffer.Affinity, ProcessorIndex);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&GroupBuffer, &GroupBufferSource, sizeof(GroupBuffer)), sizeof(GroupBuffer));
            }
        }
    }

    RtlInitUnicodeString(&Name, L"KeCountSetBitsGroupAffinity");
    CountSetBitsGroupAffinity = (PKMT_KE_COUNT_SET_BITS_GROUP_AFFINITY)MmGetSystemRoutineAddress(&Name);
    if (CountSetBitsGroupAffinity == NULL)
    {
        skip(FALSE, "KeCountSetBitsGroupAffinity is not exported\n");
        return;
    }

    for (GroupValue = 0; GroupValue <= MAXUSHORT; GroupValue++)
    {
        for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
        {
            RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(GroupValue ^ MaskIndex));
            GroupBuffer.Affinity.Mask = EnumerationMasks[MaskIndex];
            GroupBuffer.Affinity.Group = (USHORT)GroupValue;
            GroupBufferSource = GroupBuffer;
            ExpectedCount = KmtCountSetBits(GroupBuffer.Affinity.Mask);
            CountResult = CountSetBitsGroupAffinity(&GroupBuffer.Affinity);
            ok_eq_ulong(CountResult, ExpectedCount);
            ok_eq_size(RtlCompareMemory(&GroupBuffer, &GroupBufferSource, sizeof(GroupBuffer)), sizeof(GroupBuffer));
        }
    }

    for (BitNumber = 0; BitNumber < sizeof(KAFFINITY) * 8; BitNumber += 16)
    {
        for (Combination = 0; Combination <= MAXUSHORT; Combination++)
        {
            RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(BitNumber ^ Combination));
            GroupBuffer.Affinity.Mask = Combination << BitNumber;
            GroupBufferSource = GroupBuffer;
            ExpectedCount = KmtCountSetBits(GroupBuffer.Affinity.Mask);
            CountResult = CountSetBitsGroupAffinity(&GroupBuffer.Affinity);
            ok_eq_ulong(CountResult, ExpectedCount);
            ok_eq_size(RtlCompareMemory(&GroupBuffer, &GroupBufferSource, sizeof(GroupBuffer)), sizeof(GroupBuffer));
            GroupBuffer.Affinity.Mask = ~(Combination << BitNumber);
            GroupBufferSource = GroupBuffer;
            ExpectedCount = KmtCountSetBits(GroupBuffer.Affinity.Mask);
            CountResult = CountSetBitsGroupAffinity(&GroupBuffer.Affinity);
            ok_eq_ulong(CountResult, ExpectedCount);
            ok_eq_size(RtlCompareMemory(&GroupBuffer, &GroupBufferSource, sizeof(GroupBuffer)), sizeof(GroupBuffer));
        }
    }

    Combination = (KAFFINITY)0x9E3779B97F4A7C15ULL;
    for (CountIndex = 0; CountIndex < 0x100000; CountIndex++)
    {
        Combination ^= Combination << 13;
        Combination ^= Combination >> 7;
        Combination ^= Combination << 17;
        RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)CountIndex);
        GroupBuffer.Affinity.Mask = Combination;
        GroupBuffer.Affinity.Group = (USHORT)CountIndex;
        GroupBufferSource = GroupBuffer;
        ExpectedCount = KmtCountSetBits(GroupBuffer.Affinity.Mask);
        CountResult = CountSetBitsGroupAffinity(&GroupBuffer.Affinity);
        ok_eq_ulong(CountResult, ExpectedCount);
        ok_eq_size(RtlCompareMemory(&GroupBuffer, &GroupBufferSource, sizeof(GroupBuffer)), sizeof(GroupBuffer));
    }

    RtlInitUnicodeString(&Name, L"KeAndGroupAffinityEx");
    AndGroupAffinityEx = (PKMT_KE_AND_GROUP_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (AndGroupAffinityEx == NULL)
    {
        skip(FALSE, "KeAndGroupAffinityEx is not exported\n");
        return;
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(GroupAffinityCounts); CountIndex++)
    {
        RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(0x3C ^ CountIndex));
        AffinityEx2Buffer.Affinity.Count = GroupAffinityCounts[CountIndex];
        AffinityEx2Buffer.Affinity.Size = KMT_AFFINITY_EX2_GROUPS;
        RemainingAffinity = (KAFFINITY)0xD1B54A32D192ED03ULL ^ CountIndex;
        for (MaskIndex = 0; MaskIndex < KMT_AFFINITY_EX2_GROUPS; MaskIndex++)
        {
            RemainingAffinity ^= RemainingAffinity << 13;
            RemainingAffinity ^= RemainingAffinity >> 7;
            RemainingAffinity ^= RemainingAffinity << 17;
            AffinityEx2Buffer.Affinity.Bitmap[MaskIndex] = RemainingAffinity;
        }
        AffinityEx2Source = AffinityEx2Buffer;
        Combination = (KAFFINITY)0x9E3779B97F4A7C15ULL ^ CountIndex;

        for (GroupValue = 0; GroupValue <= MAXUSHORT; GroupValue++)
        {
            Combination ^= Combination << 13;
            Combination ^= Combination >> 7;
            Combination ^= Combination << 17;
            RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(CountIndex ^ GroupValue));
            GroupBuffer.Affinity.Mask = Combination;
            GroupBuffer.Affinity.Group = (USHORT)GroupValue;
            GroupBufferSource = GroupBuffer;
            RtlFillMemory(&GroupResultBuffer, sizeof(GroupResultBuffer), (UCHAR)(0xA5 ^ GroupValue));
            GroupResultExpected = GroupResultBuffer;
            RtlZeroMemory(&GroupResultExpected.Affinity, sizeof(GroupResultExpected.Affinity));
            GroupResultExpected.Affinity.Group = (USHORT)GroupValue;
            if (GroupValue < GroupAffinityCounts[CountIndex])
                GroupResultExpected.Affinity.Mask = AffinityEx2Buffer.Affinity.Bitmap[GroupValue] & Combination;
            ExpectedLogical = GroupResultExpected.Affinity.Mask != 0;
            LogicalResult = AndGroupAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, &GroupBuffer.Affinity, &GroupResultBuffer.Affinity);
            ok_eq_ulong(LogicalResult, ExpectedLogical);
            ok_eq_size(RtlCompareMemory(&GroupBuffer, &GroupBufferSource, sizeof(GroupBuffer)), sizeof(GroupBuffer));
            ok_eq_size(RtlCompareMemory(&GroupResultBuffer, &GroupResultExpected, sizeof(GroupResultBuffer)), sizeof(GroupResultBuffer));
            LogicalResult = AndGroupAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, &GroupBuffer.Affinity, NULL);
            ok_eq_ulong(LogicalResult, ExpectedLogical);
        }

        ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));

        for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
        {
            for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
            {
                RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(CountIndex ^ GroupValue ^ MaskIndex));
                GroupBuffer.Affinity.Mask = EnumerationMasks[MaskIndex];
                GroupBuffer.Affinity.Group = (USHORT)GroupValue;
                GroupBufferSource = GroupBuffer;
                RtlZeroMemory(&GroupBufferSource.Affinity, sizeof(GroupBufferSource.Affinity));
                GroupBufferSource.Affinity.Group = (USHORT)GroupValue;
                if (GroupValue < GroupAffinityCounts[CountIndex])
                    GroupBufferSource.Affinity.Mask = AffinityEx2Buffer.Affinity.Bitmap[GroupValue] & EnumerationMasks[MaskIndex];
                ExpectedLogical = GroupBufferSource.Affinity.Mask != 0;
                LogicalResult = AndGroupAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, &GroupBuffer.Affinity, &GroupBuffer.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&GroupBuffer, &GroupBufferSource, sizeof(GroupBuffer)), sizeof(GroupBuffer));
            }
        }

        ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
    }

    RtlInitUnicodeString(&Name, L"KeComplementAffinityEx");
    ComplementAffinityEx = (PKMT_KE_COMPLEMENT_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (ComplementAffinityEx == NULL)
    {
        skip(FALSE, "KeComplementAffinityEx is not exported\n");
        return;
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(ComplementAffinityCounts); CountIndex++)
    {
        for (PatternIndex = 0; PatternIndex <= MAXUSHORT; PatternIndex++)
        {
            RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ PatternIndex));
            AffinityEx2Buffer.Affinity.Count = ComplementAffinityCounts[CountIndex];
            AffinityEx2Buffer.Affinity.Size = (USHORT)(PatternIndex ^ 0xA55A);
            AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
            for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
            {
                Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U);
                Combination = (KAFFINITY)Pattern;
                Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
            }
            AffinityEx2Source = AffinityEx2Buffer;

            RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ PatternIndex));
            AffinityEx2ResultExpected = AffinityEx2Result;
            AffinityEx2ResultExpected.Affinity.Count = KAFFINITY_EX_INITIALIZED_GROUPS;
            AffinityEx2ResultExpected.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
            AffinityEx2ResultExpected.Affinity.Reserved = 0;
            for (GroupValue = 0; GroupValue < KAFFINITY_EX_INITIALIZED_GROUPS; GroupValue++)
            {
                if (GroupValue < ComplementAffinityCounts[CountIndex])
                    AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ~AffinityEx2Buffer.Affinity.Bitmap[GroupValue];
                else
                    AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ~(KAFFINITY)0;
            }

            ComplementAffinityEx((PKAFFINITY_EX)&AffinityEx2Result.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));

            AffinityEx2AliasExpected = AffinityEx2Source;
            AffinityEx2AliasExpected.Affinity.Count = KAFFINITY_EX_INITIALIZED_GROUPS;
            AffinityEx2AliasExpected.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
            AffinityEx2AliasExpected.Affinity.Reserved = 0;
            for (GroupValue = 0; GroupValue < KAFFINITY_EX_INITIALIZED_GROUPS; GroupValue++)
            {
                if (GroupValue < ComplementAffinityCounts[CountIndex])
                    AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = ~AffinityEx2Source.Affinity.Bitmap[GroupValue];
                else
                    AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = ~(KAFFINITY)0;
            }

            ComplementAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
        }
    }

    RtlInitUnicodeString(&Name, L"KeComplementAffinityEx2");
    ComplementAffinityEx2 = (PKMT_KE_COMPLEMENT_AFFINITY_EX2)MmGetSystemRoutineAddress(&Name);
    if (ComplementAffinityEx2 == NULL)
    {
        skip(FALSE, "KeComplementAffinityEx2 is not exported\n");
        return;
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(ComplementAffinityCounts); CountIndex++)
    {
        AffinityCount1 = ComplementAffinityCounts[CountIndex];
        for (SizeIndex = 0; SizeIndex < RTL_NUMBER_OF(AffinityEx2Sizes); SizeIndex++)
        {
            ResultSize = AffinityEx2Sizes[SizeIndex];
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ SizeIndex ^ PatternIndex));
                AffinityEx2Buffer.Affinity.Count = AffinityCount1;
                AffinityEx2Buffer.Affinity.Size = AffinityEx2Sizes[(CountIndex + SizeIndex + 1) % RTL_NUMBER_OF(AffinityEx2Sizes)];
                AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
                for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
                {
                    Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U + SizeIndex * 0x55U);
                    Combination = (KAFFINITY)Pattern;
                    Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                    Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                    Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                    AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
                }
                AffinityEx2Source = AffinityEx2Buffer;

                RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ SizeIndex ^ PatternIndex));
                AffinityEx2Result.Affinity.Size = ResultSize;
                AffinityEx2ResultExpected = AffinityEx2Result;
                AffinityEx2ResultExpected.Affinity.Count = ResultSize;
                AffinityEx2ResultExpected.Affinity.Size = ResultSize;
                AffinityEx2ResultExpected.Affinity.Reserved = 0;
                for (GroupValue = 0; GroupValue < ResultSize; GroupValue++)
                {
                    if (GroupValue < AffinityCount1)
                        AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ~AffinityEx2Buffer.Affinity.Bitmap[GroupValue];
                    else
                        AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ~(KAFFINITY)0;
                }
                ComplementAffinityEx2((PKAFFINITY_EX)&AffinityEx2Result.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));

                AffinityEx2AliasExpected = AffinityEx2Source;
                AliasSize = AffinityEx2Source.Affinity.Size;
                AffinityEx2AliasExpected.Affinity.Count = AliasSize;
                AffinityEx2AliasExpected.Affinity.Size = AliasSize;
                AffinityEx2AliasExpected.Affinity.Reserved = 0;
                for (GroupValue = 0; GroupValue < AliasSize; GroupValue++)
                {
                    if (GroupValue < AffinityCount1)
                        AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = ~AffinityEx2Source.Affinity.Bitmap[GroupValue];
                    else
                        AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = ~(KAFFINITY)0;
                }
                ComplementAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
            }
        }
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(ComplementAffinityEx2ExhaustivePairs); CountIndex++)
    {
        AffinityCount1 = ComplementAffinityEx2ExhaustivePairs[CountIndex][0];
        ResultSize = ComplementAffinityEx2ExhaustivePairs[CountIndex][1];
        for (PatternIndex = 0; PatternIndex <= MAXUSHORT; PatternIndex++)
        {
            RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ PatternIndex));
            AffinityEx2Buffer.Affinity.Count = AffinityCount1;
            AffinityEx2Buffer.Affinity.Size = (USHORT)((PatternIndex + CountIndex) % (KMT_AFFINITY_EX2_GROUPS + 1));
            AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
            for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
            {
                Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U);
                Combination = (KAFFINITY)Pattern;
                Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
            }
            AffinityEx2Source = AffinityEx2Buffer;
            RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ PatternIndex));
            AffinityEx2Result.Affinity.Size = ResultSize;
            AffinityEx2ResultExpected = AffinityEx2Result;
            AffinityEx2ResultExpected.Affinity.Count = ResultSize;
            AffinityEx2ResultExpected.Affinity.Size = ResultSize;
            AffinityEx2ResultExpected.Affinity.Reserved = 0;
            for (GroupValue = 0; GroupValue < ResultSize; GroupValue++)
            {
                if (GroupValue < AffinityCount1)
                    AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ~AffinityEx2Buffer.Affinity.Bitmap[GroupValue];
                else
                    AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ~(KAFFINITY)0;
            }
            ComplementAffinityEx2((PKAFFINITY_EX)&AffinityEx2Result.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));
        }
    }

    RtlInitUnicodeString(&Name, L"KeCopyAffinityEx2");
    CopyAffinityEx2 = (PKMT_KE_COPY_AFFINITY_EX2)MmGetSystemRoutineAddress(&Name);
    if (CopyAffinityEx2 == NULL)
    {
        skip(FALSE, "KeCopyAffinityEx2 is not exported\n");
        return;
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(ComplementAffinityCounts); CountIndex++)
    {
        AffinityCount1 = ComplementAffinityCounts[CountIndex];
        for (SizeIndex = 0; SizeIndex < RTL_NUMBER_OF(AffinityEx2Sizes); SizeIndex++)
        {
            ResultSize = AffinityEx2Sizes[SizeIndex];
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ SizeIndex ^ PatternIndex));
                AffinityEx2Buffer.Affinity.Count = AffinityCount1;
                AffinityEx2Buffer.Affinity.Size = AffinityEx2Sizes[(CountIndex + SizeIndex + 1) % RTL_NUMBER_OF(AffinityEx2Sizes)];
                AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
                for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
                {
                    Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U + SizeIndex * 0x55U);
                    Combination = (KAFFINITY)Pattern;
                    Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                    Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                    Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                    AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
                }
                AffinityEx2Source = AffinityEx2Buffer;

                RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ SizeIndex ^ PatternIndex));
                AffinityEx2Result.Affinity.Size = ResultSize;
                AffinityEx2ResultExpected = AffinityEx2Result;
                ResultCount = min(AffinityCount1, ResultSize);
                AffinityEx2ResultExpected.Affinity.Count = ResultCount;
                AffinityEx2ResultExpected.Affinity.Size = ResultSize;
                AffinityEx2ResultExpected.Affinity.Reserved = 0;
                for (GroupValue = 0; GroupValue < ResultSize; GroupValue++)
                {
                    if (GroupValue < ResultCount)
                        AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = AffinityEx2Buffer.Affinity.Bitmap[GroupValue];
                    else
                        AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = 0;
                }
                CopyAffinityEx2((PKAFFINITY_EX)&AffinityEx2Result.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));

                AffinityEx2AliasExpected = AffinityEx2Source;
                AliasSize = AffinityEx2Source.Affinity.Size;
                AliasCount = min(AffinityCount1, AliasSize);
                AffinityEx2AliasExpected.Affinity.Count = AliasCount;
                AffinityEx2AliasExpected.Affinity.Size = AliasSize;
                AffinityEx2AliasExpected.Affinity.Reserved = 0;
                for (GroupValue = 0; GroupValue < AliasSize; GroupValue++)
                {
                    if (GroupValue < AliasCount)
                        AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = AffinityEx2Source.Affinity.Bitmap[GroupValue];
                    else
                        AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = 0;
                }
                CopyAffinityEx2((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
            }
        }
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(CopyAffinityEx2ExhaustivePairs); CountIndex++)
    {
        AffinityCount1 = CopyAffinityEx2ExhaustivePairs[CountIndex][0];
        ResultSize = CopyAffinityEx2ExhaustivePairs[CountIndex][1];
        for (PatternIndex = 0; PatternIndex <= MAXUSHORT; PatternIndex++)
        {
            RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ PatternIndex));
            AffinityEx2Buffer.Affinity.Count = AffinityCount1;
            AffinityEx2Buffer.Affinity.Size = (USHORT)((PatternIndex + CountIndex) % (KMT_AFFINITY_EX2_GROUPS + 1));
            AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
            for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
            {
                Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U);
                Combination = (KAFFINITY)Pattern;
                Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
            }
            AffinityEx2Source = AffinityEx2Buffer;
            RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ PatternIndex));
            AffinityEx2Result.Affinity.Size = ResultSize;
            AffinityEx2ResultExpected = AffinityEx2Result;
            ResultCount = min(AffinityCount1, ResultSize);
            AffinityEx2ResultExpected.Affinity.Count = ResultCount;
            AffinityEx2ResultExpected.Affinity.Size = ResultSize;
            AffinityEx2ResultExpected.Affinity.Reserved = 0;
            for (GroupValue = 0; GroupValue < ResultSize; GroupValue++)
            {
                if (GroupValue < ResultCount)
                    AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = AffinityEx2Buffer.Affinity.Bitmap[GroupValue];
                else
                    AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = 0;
            }
            CopyAffinityEx2((PKAFFINITY_EX)&AffinityEx2Result.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));
        }
    }

    RtlInitUnicodeString(&Name, L"KeSubtractAffinityEx");
    SubtractAffinityEx = (PKMT_KE_SUBTRACT_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (SubtractAffinityEx == NULL)
    {
        skip(FALSE, "KeSubtractAffinityEx is not exported\n");
        return;
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(ComplementAffinityCounts); CountIndex++)
    {
        AffinityCount1 = ComplementAffinityCounts[CountIndex];
        for (CountIndex2 = 0; CountIndex2 < RTL_NUMBER_OF(ComplementAffinityCounts); CountIndex2++)
        {
            AffinityCount2 = ComplementAffinityCounts[CountIndex2];
            for (PatternIndex = 0; PatternIndex < 0x100; PatternIndex++)
            {
                RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ PatternIndex));
                AffinityEx2Buffer.Affinity.Count = AffinityCount1;
                AffinityEx2Buffer.Affinity.Size = (USHORT)(0xA55A ^ PatternIndex);
                AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
                RtlFillMemory(&AffinityEx2Buffer2, sizeof(AffinityEx2Buffer2), (UCHAR)(CountIndex2 ^ PatternIndex ^ 0x5A));
                AffinityEx2Buffer2.Affinity.Count = AffinityCount2;
                AffinityEx2Buffer2.Affinity.Size = (USHORT)(0x5AA5 ^ PatternIndex);
                AffinityEx2Buffer2.Affinity.Reserved = 0x0F1E2D3CUL ^ PatternIndex;
                for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
                {
                    Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U);
                    Combination = (KAFFINITY)Pattern;
                    Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                    Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                    Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                    AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
                    Pattern = (USHORT)(PatternIndex * 0xB529U + GroupValue * 0x68E3U + CountIndex2 * 0x55U);
                    RemainingAffinity = (KAFFINITY)(USHORT)(Pattern ^ 0xA5A5U);
                    RemainingAffinity |= (KAFFINITY)(USHORT)(Pattern * 0x7F4BU) << 16;
                    RemainingAffinity |= (KAFFINITY)(USHORT)((Pattern << 3) | (Pattern >> 13)) << 32;
                    RemainingAffinity |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 48;
                    AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = RemainingAffinity;
                }
                AffinityEx2Source = AffinityEx2Buffer;
                AffinityEx2Source2 = AffinityEx2Buffer2;

                RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ CountIndex2 ^ PatternIndex));
                AffinityEx2ResultExpected = AffinityEx2Result;
                ResultCount = min(AffinityCount1, KAFFINITY_EX_INITIALIZED_GROUPS);
                AffinityEx2ResultExpected.Affinity.Count = ResultCount;
                AffinityEx2ResultExpected.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
                AffinityEx2ResultExpected.Affinity.Reserved = 0;
                ExpectedLogical = FALSE;
                for (GroupValue = 0; GroupValue < KAFFINITY_EX_INITIALIZED_GROUPS; GroupValue++)
                {
                    ExpectedMask = 0;
                    if (GroupValue < ResultCount)
                    {
                        ExpectedMask = AffinityEx2Buffer.Affinity.Bitmap[GroupValue];
                        if (GroupValue < AffinityCount2)
                            ExpectedMask &= ~AffinityEx2Buffer2.Affinity.Bitmap[GroupValue];
                    }
                    AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ExpectedMask;
                    if (ExpectedMask != 0)
                        ExpectedLogical = TRUE;
                }

                LogicalResult = SubtractAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Result.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));

                AffinityEx2AliasExpected = AffinityEx2Source;
                AffinityEx2AliasExpected.Affinity.Count = ResultCount;
                AffinityEx2AliasExpected.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
                AffinityEx2AliasExpected.Affinity.Reserved = 0;
                for (GroupValue = 0; GroupValue < KAFFINITY_EX_INITIALIZED_GROUPS; GroupValue++)
                    AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue];
                LogicalResult = SubtractAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));

                AffinityEx2Buffer = AffinityEx2Source;
                AffinityEx2Buffer2 = AffinityEx2Source2;
                AffinityEx2AliasExpected = AffinityEx2Source2;
                AffinityEx2AliasExpected.Affinity.Count = ResultCount;
                AffinityEx2AliasExpected.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
                AffinityEx2AliasExpected.Affinity.Reserved = 0;
                for (GroupValue = 0; GroupValue < KAFFINITY_EX_INITIALIZED_GROUPS; GroupValue++)
                    AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue];
                LogicalResult = SubtractAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));

                if (CountIndex == CountIndex2)
                {
                    AffinityEx2Buffer = AffinityEx2Source;
                    AffinityEx2AliasExpected = AffinityEx2Source;
                    AffinityEx2AliasExpected.Affinity.Count = ResultCount;
                    AffinityEx2AliasExpected.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
                    AffinityEx2AliasExpected.Affinity.Reserved = 0;
                    for (GroupValue = 0; GroupValue < KAFFINITY_EX_INITIALIZED_GROUPS; GroupValue++)
                        AffinityEx2AliasExpected.Affinity.Bitmap[GroupValue] = 0;
                    LogicalResult = SubtractAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
                    ok_eq_ulong(LogicalResult, FALSE);
                    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2AliasExpected, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                }
            }
        }
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(SubtractAffinityExhaustivePairs); CountIndex++)
    {
        AffinityCount1 = SubtractAffinityExhaustivePairs[CountIndex][0];
        AffinityCount2 = SubtractAffinityExhaustivePairs[CountIndex][1];
        for (PatternIndex = 0; PatternIndex <= MAXUSHORT; PatternIndex++)
        {
            RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ PatternIndex));
            AffinityEx2Buffer.Affinity.Count = AffinityCount1;
            AffinityEx2Buffer.Affinity.Size = (USHORT)(0xA55A ^ PatternIndex);
            AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ PatternIndex;
            RtlFillMemory(&AffinityEx2Buffer2, sizeof(AffinityEx2Buffer2), (UCHAR)(CountIndex ^ PatternIndex ^ 0x5A));
            AffinityEx2Buffer2.Affinity.Count = AffinityCount2;
            AffinityEx2Buffer2.Affinity.Size = (USHORT)(0x5AA5 ^ PatternIndex);
            AffinityEx2Buffer2.Affinity.Reserved = 0x0F1E2D3CUL ^ PatternIndex;
            for (GroupValue = 0; GroupValue < KMT_AFFINITY_EX2_GROUPS; GroupValue++)
            {
                Pattern = (USHORT)(PatternIndex + GroupValue * 0x9E37U + CountIndex * 0x31U);
                Combination = (KAFFINITY)Pattern;
                Combination |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 16;
                Combination |= (KAFFINITY)(USHORT)(Pattern * 0x9E37U) << 32;
                Combination |= (KAFFINITY)(USHORT)((Pattern << 1) | (Pattern >> 15)) << 48;
                AffinityEx2Buffer.Affinity.Bitmap[GroupValue] = Combination;
                Pattern = (USHORT)(PatternIndex * 0xB529U + GroupValue * 0x68E3U + CountIndex * 0x55U);
                RemainingAffinity = (KAFFINITY)(USHORT)(Pattern ^ 0xA5A5U);
                RemainingAffinity |= (KAFFINITY)(USHORT)(Pattern * 0x7F4BU) << 16;
                RemainingAffinity |= (KAFFINITY)(USHORT)((Pattern << 3) | (Pattern >> 13)) << 32;
                RemainingAffinity |= (KAFFINITY)(USHORT)(Pattern ^ MAXUSHORT) << 48;
                AffinityEx2Buffer2.Affinity.Bitmap[GroupValue] = RemainingAffinity;
            }
            AffinityEx2Source = AffinityEx2Buffer;
            AffinityEx2Source2 = AffinityEx2Buffer2;
            RtlFillMemory(&AffinityEx2Result, sizeof(AffinityEx2Result), (UCHAR)(0xA5 ^ CountIndex ^ PatternIndex));
            AffinityEx2ResultExpected = AffinityEx2Result;
            ResultCount = min(AffinityCount1, KAFFINITY_EX_INITIALIZED_GROUPS);
            AffinityEx2ResultExpected.Affinity.Count = ResultCount;
            AffinityEx2ResultExpected.Affinity.Size = KAFFINITY_EX_INITIALIZED_GROUPS;
            AffinityEx2ResultExpected.Affinity.Reserved = 0;
            ExpectedLogical = FALSE;
            for (GroupValue = 0; GroupValue < KAFFINITY_EX_INITIALIZED_GROUPS; GroupValue++)
            {
                ExpectedMask = 0;
                if (GroupValue < ResultCount)
                {
                    ExpectedMask = AffinityEx2Buffer.Affinity.Bitmap[GroupValue];
                    if (GroupValue < AffinityCount2)
                        ExpectedMask &= ~AffinityEx2Buffer2.Affinity.Bitmap[GroupValue];
                }
                AffinityEx2ResultExpected.Affinity.Bitmap[GroupValue] = ExpectedMask;
                if (ExpectedMask != 0)
                    ExpectedLogical = TRUE;
            }
            LogicalResult = SubtractAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, (PKAFFINITY_EX)&AffinityEx2Result.Affinity);
            ok_eq_ulong(LogicalResult, ExpectedLogical);
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Result, &AffinityEx2ResultExpected, sizeof(AffinityEx2Result)), sizeof(AffinityEx2Result));
        }
    }

    for (CountIndex = 0; CountIndex <= KMT_AFFINITY_EX2_GROUPS; CountIndex++)
    {
        for (CountIndex2 = 0; CountIndex2 <= KMT_AFFINITY_EX2_GROUPS; CountIndex2++)
        {
            for (ProbeGroup = 0; ProbeGroup < KMT_AFFINITY_EX2_GROUPS; ProbeGroup++)
            {
                RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), (UCHAR)(CountIndex ^ ProbeGroup));
                AffinityEx2Buffer.Affinity.Count = (USHORT)CountIndex;
                AffinityEx2Buffer.Affinity.Size = (USHORT)(0xA55A ^ ProbeGroup);
                AffinityEx2Buffer.Affinity.Reserved = 0xC3D2E1F0UL ^ ProbeGroup;
                RtlZeroMemory(AffinityEx2Buffer.Affinity.Bitmap, sizeof(AffinityEx2Buffer.Affinity.Bitmap));
                RtlFillMemory(&AffinityEx2Buffer2, sizeof(AffinityEx2Buffer2), (UCHAR)(CountIndex2 ^ ProbeGroup ^ 0x5A));
                AffinityEx2Buffer2.Affinity.Count = (USHORT)CountIndex2;
                AffinityEx2Buffer2.Affinity.Size = (USHORT)(0x5AA5 ^ ProbeGroup);
                AffinityEx2Buffer2.Affinity.Reserved = 0x0F1E2D3CUL ^ ProbeGroup;
                RtlZeroMemory(AffinityEx2Buffer2.Affinity.Bitmap, sizeof(AffinityEx2Buffer2.Affinity.Bitmap));
                ExpectedMask = EnumerationMasks[1 + ((CountIndex + CountIndex2 + ProbeGroup) % (RTL_NUMBER_OF(EnumerationMasks) - 1))];
                AffinityEx2Buffer.Affinity.Bitmap[ProbeGroup] = ExpectedMask;
                if ((CountIndex ^ CountIndex2 ^ ProbeGroup) & 1)
                    AffinityEx2Buffer2.Affinity.Bitmap[ProbeGroup] = ExpectedMask;
                else
                    AffinityEx2Buffer2.Affinity.Bitmap[ProbeGroup] = ~ExpectedMask;
                AffinityEx2Source = AffinityEx2Buffer;
                AffinityEx2Source2 = AffinityEx2Buffer2;
                if (ProbeGroup < CountIndex)
                {
                    if (ProbeGroup < CountIndex2)
                        ExpectedMask &= ~AffinityEx2Buffer2.Affinity.Bitmap[ProbeGroup];
                    ExpectedLogical = ExpectedMask != 0;
                }
                else
                {
                    ExpectedLogical = FALSE;
                }
                LogicalResult = SubtractAffinityEx((PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, (PKAFFINITY_EX)&AffinityEx2Buffer2.Affinity, NULL);
                ok_eq_ulong(LogicalResult, ExpectedLogical);
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer2, &AffinityEx2Source2, sizeof(AffinityEx2Buffer2)), sizeof(AffinityEx2Buffer2));
            }
        }
    }

    RtlInitUnicodeString(&Name, L"KeSubtractAffinityEx2");
    SubtractAffinityEx2 = (PKMT_KE_SUBTRACT_AFFINITY_EX2)MmGetSystemRoutineAddress(&Name);
    if (SubtractAffinityEx2 == NULL)
    {
        skip(FALSE, "KeSubtractAffinityEx2 is not exported\n");
        return;
    }
    KmtTestSubtractAffinityEx2(SubtractAffinityEx2);

    RtlInitUnicodeString(&Name, L"KeInterlockedSetProcessorAffinityEx");
    InterlockedSetProcessorAffinityEx = (PKMT_KE_INTERLOCKED_SET_PROCESSOR_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (InterlockedSetProcessorAffinityEx == NULL)
    {
        skip(FALSE, "KeInterlockedSetProcessorAffinityEx is not exported\n");
        return;
    }
    KmtTestInterlockedSetProcessorAffinityEx(InterlockedSetProcessorAffinityEx, GetProcessorNumberFromIndex);

    RtlInitUnicodeString(&Name, L"KeRemoveProcessorGroupAffinity");
    RemoveProcessorGroupAffinity = (PKMT_KE_REMOVE_PROCESSOR_GROUP_AFFINITY)MmGetSystemRoutineAddress(&Name);
    if (RemoveProcessorGroupAffinity == NULL)
    {
        skip(FALSE, "KeRemoveProcessorGroupAffinity is not exported\n");
        return;
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
        for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
        {
            RtlFillMemory(&GroupBuffer, sizeof(GroupBuffer), (UCHAR)(ProcessorIndex ^ MaskIndex));
            GroupBuffer.Affinity.Mask = EnumerationMasks[MaskIndex];
            GroupBufferSource = GroupBuffer;
            GroupAffinity = GroupBuffer.Affinity;
            GroupAffinity.Mask &= ~((KAFFINITY)1 << ProcessorNumber.Number);
            RemoveProcessorGroupAffinity(&GroupBuffer.Affinity, ProcessorIndex);
            ok_eq_size(RtlCompareMemory(GroupBuffer.GuardBefore, GroupBufferSource.GuardBefore, sizeof(GroupBuffer.GuardBefore)), sizeof(GroupBuffer.GuardBefore));
            ok_eq_size(RtlCompareMemory(&GroupBuffer.Affinity, &GroupAffinity, sizeof(GroupAffinity)), sizeof(GroupAffinity));
            ok_eq_size(RtlCompareMemory(GroupBuffer.GuardAfter, GroupBufferSource.GuardAfter, sizeof(GroupBuffer.GuardAfter)), sizeof(GroupBuffer.GuardAfter));
        }
    }

    RtlInitUnicodeString(&Name, L"KeEnumerateNextProcessor");
    EnumerateNextProcessor = (PKMT_KE_ENUMERATE_NEXT_PROCESSOR)MmGetSystemRoutineAddress(&Name);
    if (EnumerateNextProcessor == NULL)
    {
        skip(FALSE, "KeEnumerateNextProcessor is not exported\n");
        return;
    }

    for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
    {
        RtlFillMemory(&EnumerationBuffer, sizeof(EnumerationBuffer), 0xA5);
        InitializeEnumerationContextFromAffinity(&EnumerationBuffer.Context, 0, EnumerationMasks[MaskIndex]);
        EnumerationSource = EnumerationBuffer;
        RemainingAffinity = EnumerationMasks[MaskIndex];

        while (BitScanForwardAffinity(&BitNumber, RemainingAffinity))
        {
            RemainingAffinity &= ~((KAFFINITY)1 << BitNumber);
            ProcessorNumber.Group = 0;
            ProcessorNumber.Number = (UCHAR)BitNumber;
            ProcessorNumber.Reserved = 0;
            RtlFillMemory(&ProcessorIndexBuffer, sizeof(ProcessorIndexBuffer), 0x3C);
            ProcessorIndexSource = ProcessorIndexBuffer;
            EnumerationStatus = EnumerateNextProcessor(&ProcessorIndexBuffer.ProcessorIndex, &EnumerationBuffer.Context);
            ok_eq_hex(EnumerationStatus, STATUS_SUCCESS);
            ExpectedProcessorIndex = GetProcessorIndexFromNumber(&ProcessorNumber);
            if (ExpectedProcessorIndex == INVALID_PROCESSOR_INDEX)
                ExpectedProcessorIndex = 0;
            ok_eq_ulong(ProcessorIndexBuffer.ProcessorIndex, ExpectedProcessorIndex);
            ok_eq_size(RtlCompareMemory(ProcessorIndexBuffer.GuardBefore, ProcessorIndexSource.GuardBefore, sizeof(ProcessorIndexBuffer.GuardBefore)), sizeof(ProcessorIndexBuffer.GuardBefore));
            ok_eq_size(RtlCompareMemory(ProcessorIndexBuffer.GuardAfter, ProcessorIndexSource.GuardAfter, sizeof(ProcessorIndexBuffer.GuardAfter)), sizeof(ProcessorIndexBuffer.GuardAfter));
            ok(EnumerationBuffer.Context.Affinity == NULL, "context affinity %p, expected NULL\n", EnumerationBuffer.Context.Affinity);
            ok_eq_ulonglong(EnumerationBuffer.Context.CurrentAffinity, RemainingAffinity);
            ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, 0);
            ok_eq_size(RtlCompareMemory((PUCHAR)&EnumerationBuffer.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationBuffer.Context.CurrentGroup), (PUCHAR)&EnumerationSource.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationSource.Context.CurrentGroup), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup)), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup));
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardBefore, EnumerationSource.GuardBefore, sizeof(EnumerationBuffer.GuardBefore)), sizeof(EnumerationBuffer.GuardBefore));
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardAfter, EnumerationSource.GuardAfter, sizeof(EnumerationBuffer.GuardAfter)), sizeof(EnumerationBuffer.GuardAfter));
        }

        RtlFillMemory(&ProcessorIndexBuffer, sizeof(ProcessorIndexBuffer), 0x3C);
        ProcessorIndexSource = ProcessorIndexBuffer;
        EnumerationStatus = EnumerateNextProcessor(&ProcessorIndexBuffer.ProcessorIndex, &EnumerationBuffer.Context);
        ok_eq_hex(EnumerationStatus, STATUS_NOT_FOUND);
        ok_eq_size(RtlCompareMemory(&ProcessorIndexBuffer, &ProcessorIndexSource, sizeof(ProcessorIndexBuffer)), sizeof(ProcessorIndexBuffer));
        ok(EnumerationBuffer.Context.Affinity == NULL, "context affinity %p, expected NULL\n", EnumerationBuffer.Context.Affinity);
        ok_eq_ulonglong(EnumerationBuffer.Context.CurrentAffinity, 0);
        ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, 1);
        ok_eq_size(RtlCompareMemory((PUCHAR)&EnumerationBuffer.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationBuffer.Context.CurrentGroup), (PUCHAR)&EnumerationSource.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationSource.Context.CurrentGroup), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup)), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup));
        ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardBefore, EnumerationSource.GuardBefore, sizeof(EnumerationBuffer.GuardBefore)), sizeof(EnumerationBuffer.GuardBefore));
        ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardAfter, EnumerationSource.GuardAfter, sizeof(EnumerationBuffer.GuardAfter)), sizeof(EnumerationBuffer.GuardAfter));

        RtlFillMemory(&ProcessorIndexBuffer, sizeof(ProcessorIndexBuffer), 0xC3);
        ProcessorIndexSource = ProcessorIndexBuffer;
        EnumerationStatus = EnumerateNextProcessor(&ProcessorIndexBuffer.ProcessorIndex, &EnumerationBuffer.Context);
        ok_eq_hex(EnumerationStatus, STATUS_NOT_FOUND);
        ok_eq_size(RtlCompareMemory(&ProcessorIndexBuffer, &ProcessorIndexSource, sizeof(ProcessorIndexBuffer)), sizeof(ProcessorIndexBuffer));
        ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, 2);
    }

    for (CountIndex = 0; CountIndex < RTL_NUMBER_OF(EnumerationCounts); CountIndex++)
    {
        for (MaskIndex = 0; MaskIndex < RTL_NUMBER_OF(EnumerationMasks); MaskIndex++)
        {
            RtlFillMemory(&AffinityEx2Buffer, sizeof(AffinityEx2Buffer), 0x3C);
            AffinityEx2Buffer.Affinity.Count = EnumerationCounts[CountIndex];
            AffinityEx2Buffer.Affinity.Size = (USHORT)(0xA5A5 ^ MaskIndex);
            AffinityEx2Buffer.Affinity.Reserved = 0x5AA55AA5;
            for (GroupNumber = 0; GroupNumber < KMT_AFFINITY_EX2_GROUPS; GroupNumber++)
                AffinityEx2Buffer.Affinity.Bitmap[GroupNumber] = 0;
            AffinityEx2Buffer.Affinity.Bitmap[0] = EnumerationMasks[MaskIndex];
            AffinityEx2Source = AffinityEx2Buffer;
            RtlFillMemory(&EnumerationBuffer, sizeof(EnumerationBuffer), 0xA5);
            InitializeEnumerationContext(&EnumerationBuffer.Context, (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity);
            EnumerationSource = EnumerationBuffer;
            ExpectedGroup = 0;
            RemainingAffinity = AffinityEx2Buffer.Affinity.Bitmap[0];

            for (;;)
            {
                if (BitScanForwardAffinity(&BitNumber, RemainingAffinity))
                {
                    RemainingAffinity &= ~((KAFFINITY)1 << BitNumber);
                    ProcessorNumber.Group = ExpectedGroup;
                    ProcessorNumber.Number = (UCHAR)BitNumber;
                    ProcessorNumber.Reserved = 0;
                    RtlFillMemory(&ProcessorIndexBuffer, sizeof(ProcessorIndexBuffer), 0x3C);
                    ProcessorIndexSource = ProcessorIndexBuffer;
                    EnumerationStatus = EnumerateNextProcessor(&ProcessorIndexBuffer.ProcessorIndex, &EnumerationBuffer.Context);
                    ok_eq_hex(EnumerationStatus, STATUS_SUCCESS);
                    ExpectedProcessorIndex = GetProcessorIndexFromNumber(&ProcessorNumber);
                    if (ExpectedProcessorIndex == INVALID_PROCESSOR_INDEX)
                        ExpectedProcessorIndex = 0;
                    ok_eq_ulong(ProcessorIndexBuffer.ProcessorIndex, ExpectedProcessorIndex);
                    ok_eq_size(RtlCompareMemory(ProcessorIndexBuffer.GuardBefore, ProcessorIndexSource.GuardBefore, sizeof(ProcessorIndexBuffer.GuardBefore)), sizeof(ProcessorIndexBuffer.GuardBefore));
                    ok_eq_size(RtlCompareMemory(ProcessorIndexBuffer.GuardAfter, ProcessorIndexSource.GuardAfter, sizeof(ProcessorIndexBuffer.GuardAfter)), sizeof(ProcessorIndexBuffer.GuardAfter));
                    ok(EnumerationBuffer.Context.Affinity == (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, "context affinity %p, expected %p\n", EnumerationBuffer.Context.Affinity, &AffinityEx2Buffer.Affinity);
                    ok_eq_ulonglong(EnumerationBuffer.Context.CurrentAffinity, RemainingAffinity);
                    ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, ExpectedGroup);
                    ok_eq_size(RtlCompareMemory((PUCHAR)&EnumerationBuffer.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationBuffer.Context.CurrentGroup), (PUCHAR)&EnumerationSource.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationSource.Context.CurrentGroup), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup)), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup));
                    ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardBefore, EnumerationSource.GuardBefore, sizeof(EnumerationBuffer.GuardBefore)), sizeof(EnumerationBuffer.GuardBefore));
                    ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardAfter, EnumerationSource.GuardAfter, sizeof(EnumerationBuffer.GuardAfter)), sizeof(EnumerationBuffer.GuardAfter));
                    ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
                    continue;
                }

                ExpectedGroup++;
                if (ExpectedGroup >= AffinityEx2Buffer.Affinity.Count)
                    break;
                RemainingAffinity = AffinityEx2Buffer.Affinity.Bitmap[ExpectedGroup];
            }

            RtlFillMemory(&ProcessorIndexBuffer, sizeof(ProcessorIndexBuffer), 0x3C);
            ProcessorIndexSource = ProcessorIndexBuffer;
            EnumerationStatus = EnumerateNextProcessor(&ProcessorIndexBuffer.ProcessorIndex, &EnumerationBuffer.Context);
            ok_eq_hex(EnumerationStatus, STATUS_NOT_FOUND);
            ok_eq_size(RtlCompareMemory(&ProcessorIndexBuffer, &ProcessorIndexSource, sizeof(ProcessorIndexBuffer)), sizeof(ProcessorIndexBuffer));
            ok(EnumerationBuffer.Context.Affinity == (PKAFFINITY_EX)&AffinityEx2Buffer.Affinity, "context affinity %p, expected %p\n", EnumerationBuffer.Context.Affinity, &AffinityEx2Buffer.Affinity);
            ok_eq_ulonglong(EnumerationBuffer.Context.CurrentAffinity, 0);
            ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, ExpectedGroup);
            ok_eq_size(RtlCompareMemory((PUCHAR)&EnumerationBuffer.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationBuffer.Context.CurrentGroup), (PUCHAR)&EnumerationSource.Context + FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) + sizeof(EnumerationSource.Context.CurrentGroup), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup)), sizeof(EnumerationBuffer.Context) - FIELD_OFFSET(KAFFINITY_ENUMERATION_CONTEXT, CurrentGroup) - sizeof(EnumerationBuffer.Context.CurrentGroup));
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardBefore, EnumerationSource.GuardBefore, sizeof(EnumerationBuffer.GuardBefore)), sizeof(EnumerationBuffer.GuardBefore));
            ok_eq_size(RtlCompareMemory(EnumerationBuffer.GuardAfter, EnumerationSource.GuardAfter, sizeof(EnumerationBuffer.GuardAfter)), sizeof(EnumerationBuffer.GuardAfter));
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));

            RtlFillMemory(&ProcessorIndexBuffer, sizeof(ProcessorIndexBuffer), 0xC3);
            ProcessorIndexSource = ProcessorIndexBuffer;
            EnumerationStatus = EnumerateNextProcessor(&ProcessorIndexBuffer.ProcessorIndex, &EnumerationBuffer.Context);
            ok_eq_hex(EnumerationStatus, STATUS_NOT_FOUND);
            ok_eq_size(RtlCompareMemory(&ProcessorIndexBuffer, &ProcessorIndexSource, sizeof(ProcessorIndexBuffer)), sizeof(ProcessorIndexBuffer));
            ok_eq_uint(EnumerationBuffer.Context.CurrentGroup, ExpectedGroup + 1);
            ok_eq_size(RtlCompareMemory(&AffinityEx2Buffer, &AffinityEx2Source, sizeof(AffinityEx2Buffer)), sizeof(AffinityEx2Buffer));
        }
    }

    RtlInitUnicodeString(&Name, L"KeFindFirstSetLeftAffinityEx");
    FindFirstSetLeftAffinityEx = (PKMT_KE_FIND_FIRST_SET_LEFT_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (FindFirstSetLeftAffinityEx == NULL)
    {
        skip(FALSE, "KeFindFirstSetLeftAffinityEx is not exported\n");
        return;
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Bitmap[0] = (KAFFINITY)1 << (ActiveCount - 1);
    ok_eq_ulong(FindFirstSetLeftAffinityEx(&Affinity), INVALID_PROCESSOR_INDEX);
    for (GroupNumber = 0; GroupNumber <= KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
    {
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Count = GroupNumber;
        ok_eq_ulong(FindFirstSetLeftAffinityEx(&Affinity), INVALID_PROCESSOR_INDEX);
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Count = ProcessorNumber.Group + 1;
        Affinity.Bitmap[ProcessorNumber.Group] = (KAFFINITY)1 << ProcessorNumber.Number;
        ok_eq_ulong(FindFirstSetLeftAffinityEx(&Affinity), ProcessorIndex);
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        for (OtherIndex = ProcessorIndex; OtherIndex < ActiveCount; OtherIndex++)
        {
            RtlZeroMemory(&Affinity, sizeof(Affinity));
            Affinity.Size = 0xA5A5;
            Affinity.Reserved = 0xA5A5A5A5;
            ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
            Affinity.Count = ProcessorNumber.Group + 1;
            Affinity.Bitmap[ProcessorNumber.Group] |= (KAFFINITY)1 << ProcessorNumber.Number;
            HighestIndex = ProcessorIndex;
            HighestProcessorNumber = ProcessorNumber;
            ok_eq_hex(GetProcessorNumberFromIndex(OtherIndex, &ProcessorNumber), STATUS_SUCCESS);
            if (Affinity.Count <= ProcessorNumber.Group)
                Affinity.Count = ProcessorNumber.Group + 1;
            Affinity.Bitmap[ProcessorNumber.Group] |= (KAFFINITY)1 << ProcessorNumber.Number;
            if ((ProcessorNumber.Group > HighestProcessorNumber.Group) ||
                ((ProcessorNumber.Group == HighestProcessorNumber.Group) &&
                 (ProcessorNumber.Number > HighestProcessorNumber.Number)))
            {
                HighestIndex = OtherIndex;
            }
            ok_eq_ulong(FindFirstSetLeftAffinityEx(&Affinity), HighestIndex);
        }
    }

    if (ActiveCount <= 16)
    {
        CombinationLimit = (KAFFINITY)1 << ActiveCount;
        for (Combination = 1; Combination < CombinationLimit; Combination++)
        {
            RtlZeroMemory(&Affinity, sizeof(Affinity));
            Affinity.Size = 0xA5A5;
            Affinity.Reserved = 0xA5A5A5A5;
            HighestIndex = INVALID_PROCESSOR_INDEX;
            for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
            {
                if ((Combination & ((KAFFINITY)1 << ProcessorIndex)) == 0)
                    continue;

                ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
                if (Affinity.Count <= ProcessorNumber.Group)
                    Affinity.Count = ProcessorNumber.Group + 1;
                Affinity.Bitmap[ProcessorNumber.Group] |= (KAFFINITY)1 << ProcessorNumber.Number;
                if ((HighestIndex == INVALID_PROCESSOR_INDEX) ||
                    (ProcessorNumber.Group > HighestProcessorNumber.Group) ||
                    ((ProcessorNumber.Group == HighestProcessorNumber.Group) &&
                     (ProcessorNumber.Number > HighestProcessorNumber.Number)))
                {
                    HighestIndex = ProcessorIndex;
                    HighestProcessorNumber = ProcessorNumber;
                }
            }
            ok_eq_ulong(FindFirstSetLeftAffinityEx(&Affinity), HighestIndex);
        }
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Count = 1;
    Affinity.Size = 0xA5A5;
    Affinity.Reserved = 0xA5A5A5A5;
    Affinity.Bitmap[0] = 2;
    Affinity.Bitmap[1] = 1;
    Source = Affinity;
    ok_eq_ulong(FindFirstSetLeftAffinityEx(&Affinity), 1);
    ok_eq_size(RtlCompareMemory(&Affinity, &Source, sizeof(Affinity)), sizeof(Affinity));

    RtlInitUnicodeString(&Name, L"KeFindFirstSetRightAffinityEx");
    FindFirstSetRightAffinityEx = (PKMT_KE_FIND_FIRST_SET_RIGHT_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (FindFirstSetRightAffinityEx == NULL)
    {
        skip(FALSE, "KeFindFirstSetRightAffinityEx is not exported\n");
        return;
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Bitmap[0] = (KAFFINITY)1 << (ActiveCount - 1);
    ok_eq_ulong(FindFirstSetRightAffinityEx(&Affinity), INVALID_PROCESSOR_INDEX);
    for (GroupNumber = 0; GroupNumber <= KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
    {
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Count = GroupNumber;
        ok_eq_ulong(FindFirstSetRightAffinityEx(&Affinity), INVALID_PROCESSOR_INDEX);
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Count = ProcessorNumber.Group + 1;
        Affinity.Bitmap[ProcessorNumber.Group] = (KAFFINITY)1 << ProcessorNumber.Number;
        ok_eq_ulong(FindFirstSetRightAffinityEx(&Affinity), ProcessorIndex);
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        for (OtherIndex = ProcessorIndex; OtherIndex < ActiveCount; OtherIndex++)
        {
            RtlZeroMemory(&Affinity, sizeof(Affinity));
            Affinity.Size = 0xA5A5;
            Affinity.Reserved = 0xA5A5A5A5;
            ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
            Affinity.Count = ProcessorNumber.Group + 1;
            Affinity.Bitmap[ProcessorNumber.Group] |= (KAFFINITY)1 << ProcessorNumber.Number;
            LowestIndex = ProcessorIndex;
            LowestProcessorNumber = ProcessorNumber;
            ok_eq_hex(GetProcessorNumberFromIndex(OtherIndex, &ProcessorNumber), STATUS_SUCCESS);
            if (Affinity.Count <= ProcessorNumber.Group)
                Affinity.Count = ProcessorNumber.Group + 1;
            Affinity.Bitmap[ProcessorNumber.Group] |= (KAFFINITY)1 << ProcessorNumber.Number;
            if ((ProcessorNumber.Group < LowestProcessorNumber.Group) ||
                ((ProcessorNumber.Group == LowestProcessorNumber.Group) &&
                 (ProcessorNumber.Number < LowestProcessorNumber.Number)))
            {
                LowestIndex = OtherIndex;
            }
            ok_eq_ulong(FindFirstSetRightAffinityEx(&Affinity), LowestIndex);
        }
    }

    if (ActiveCount <= 16)
    {
        CombinationLimit = (KAFFINITY)1 << ActiveCount;
        for (Combination = 1; Combination < CombinationLimit; Combination++)
        {
            RtlZeroMemory(&Affinity, sizeof(Affinity));
            Affinity.Size = 0xA5A5;
            Affinity.Reserved = 0xA5A5A5A5;
            LowestIndex = INVALID_PROCESSOR_INDEX;
            for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
            {
                if ((Combination & ((KAFFINITY)1 << ProcessorIndex)) == 0)
                    continue;

                ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
                if (Affinity.Count <= ProcessorNumber.Group)
                    Affinity.Count = ProcessorNumber.Group + 1;
                Affinity.Bitmap[ProcessorNumber.Group] |= (KAFFINITY)1 << ProcessorNumber.Number;
                if ((LowestIndex == INVALID_PROCESSOR_INDEX) ||
                    (ProcessorNumber.Group < LowestProcessorNumber.Group) ||
                    ((ProcessorNumber.Group == LowestProcessorNumber.Group) &&
                     (ProcessorNumber.Number < LowestProcessorNumber.Number)))
                {
                    LowestIndex = ProcessorIndex;
                    LowestProcessorNumber = ProcessorNumber;
                }
            }
            ok_eq_ulong(FindFirstSetRightAffinityEx(&Affinity), LowestIndex);
        }
    }

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Count = 1;
    Affinity.Size = 0xA5A5;
    Affinity.Reserved = 0xA5A5A5A5;
    Affinity.Bitmap[0] = 2;
    Affinity.Bitmap[1] = 1;
    Source = Affinity;
    ok_eq_ulong(FindFirstSetRightAffinityEx(&Affinity), 1);
    ok_eq_size(RtlCompareMemory(&Affinity, &Source, sizeof(Affinity)), sizeof(Affinity));

    RtlInitUnicodeString(&Name, L"KeFindFirstSetLeftGroupAffinity");
    FindFirstSetLeftGroupAffinity = (PKMT_KE_FIND_FIRST_SET_LEFT_GROUP_AFFINITY)MmGetSystemRoutineAddress(&Name);
    if (FindFirstSetLeftGroupAffinity == NULL)
    {
        skip(FALSE, "KeFindFirstSetLeftGroupAffinity is not exported\n");
        return;
    }

    RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
    GroupAffinity.Mask = 0;
    GroupSource = GroupAffinity;
    ok_eq_ulong(FindFirstSetLeftGroupAffinity(&GroupAffinity), INVALID_PROCESSOR_INDEX);
    ok_eq_size(RtlCompareMemory(&GroupAffinity, &GroupSource, sizeof(GroupAffinity)), sizeof(GroupAffinity));

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
        RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
        GroupAffinity.Mask = (KAFFINITY)1 << ProcessorNumber.Number;
        GroupAffinity.Group = ProcessorNumber.Group;
        ok_eq_ulong(FindFirstSetLeftGroupAffinity(&GroupAffinity), ProcessorIndex);
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        for (OtherIndex = ProcessorIndex; OtherIndex < ActiveCount; OtherIndex++)
        {
            ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
            RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
            GroupAffinity.Mask = (KAFFINITY)1 << ProcessorNumber.Number;
            GroupAffinity.Group = ProcessorNumber.Group;
            HighestIndex = ProcessorIndex;
            HighestProcessorNumber = ProcessorNumber;
            ok_eq_hex(GetProcessorNumberFromIndex(OtherIndex, &ProcessorNumber), STATUS_SUCCESS);
            if (ProcessorNumber.Group != GroupAffinity.Group)
                continue;
            GroupAffinity.Mask |= (KAFFINITY)1 << ProcessorNumber.Number;
            if (ProcessorNumber.Number > HighestProcessorNumber.Number)
                HighestIndex = OtherIndex;
            ok_eq_ulong(FindFirstSetLeftGroupAffinity(&GroupAffinity), HighestIndex);
        }
    }

    if (ActiveCount <= 16)
    {
        CombinationLimit = (KAFFINITY)1 << ActiveCount;
        for (Combination = 1; Combination < CombinationLimit; Combination++)
        {
            RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
            GroupAffinity.Mask = 0;
            HighestIndex = INVALID_PROCESSOR_INDEX;
            for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
            {
                if ((Combination & ((KAFFINITY)1 << ProcessorIndex)) == 0)
                    continue;

                ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
                if (HighestIndex == INVALID_PROCESSOR_INDEX)
                {
                    GroupAffinity.Group = ProcessorNumber.Group;
                    HighestIndex = ProcessorIndex;
                    HighestProcessorNumber = ProcessorNumber;
                }
                else if (ProcessorNumber.Group != GroupAffinity.Group)
                {
                    HighestIndex = INVALID_PROCESSOR_INDEX;
                    break;
                }
                GroupAffinity.Mask |= (KAFFINITY)1 << ProcessorNumber.Number;
                if (ProcessorNumber.Number > HighestProcessorNumber.Number)
                {
                    HighestIndex = ProcessorIndex;
                    HighestProcessorNumber = ProcessorNumber;
                }
            }
            if (HighestIndex != INVALID_PROCESSOR_INDEX)
                ok_eq_ulong(FindFirstSetLeftGroupAffinity(&GroupAffinity), HighestIndex);
        }
    }

    ok_eq_hex(GetProcessorNumberFromIndex(0, &ProcessorNumber), STATUS_SUCCESS);
    RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
    GroupAffinity.Mask = (KAFFINITY)1 << ProcessorNumber.Number;
    GroupAffinity.Group = ProcessorNumber.Group;
    GroupSource = GroupAffinity;
    ok_eq_ulong(FindFirstSetLeftGroupAffinity(&GroupAffinity), 0);
    ok_eq_size(RtlCompareMemory(&GroupAffinity, &GroupSource, sizeof(GroupAffinity)), sizeof(GroupAffinity));

    RtlInitUnicodeString(&Name, L"KeFindFirstSetRightGroupAffinity");
    FindFirstSetRightGroupAffinity = (PKMT_KE_FIND_FIRST_SET_RIGHT_GROUP_AFFINITY)MmGetSystemRoutineAddress(&Name);
    if (FindFirstSetRightGroupAffinity == NULL)
    {
        skip(FALSE, "KeFindFirstSetRightGroupAffinity is not exported\n");
        return;
    }

    RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
    GroupAffinity.Mask = 0;
    GroupSource = GroupAffinity;
    ok_eq_ulong(FindFirstSetRightGroupAffinity(&GroupAffinity), INVALID_PROCESSOR_INDEX);
    ok_eq_size(RtlCompareMemory(&GroupAffinity, &GroupSource, sizeof(GroupAffinity)), sizeof(GroupAffinity));

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
        RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
        GroupAffinity.Mask = (KAFFINITY)1 << ProcessorNumber.Number;
        GroupAffinity.Group = ProcessorNumber.Group;
        ok_eq_ulong(FindFirstSetRightGroupAffinity(&GroupAffinity), ProcessorIndex);
    }

    for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
    {
        for (OtherIndex = ProcessorIndex; OtherIndex < ActiveCount; OtherIndex++)
        {
            ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
            RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
            GroupAffinity.Mask = (KAFFINITY)1 << ProcessorNumber.Number;
            GroupAffinity.Group = ProcessorNumber.Group;
            LowestIndex = ProcessorIndex;
            LowestProcessorNumber = ProcessorNumber;
            ok_eq_hex(GetProcessorNumberFromIndex(OtherIndex, &ProcessorNumber), STATUS_SUCCESS);
            if (ProcessorNumber.Group != GroupAffinity.Group)
                continue;
            GroupAffinity.Mask |= (KAFFINITY)1 << ProcessorNumber.Number;
            if (ProcessorNumber.Number < LowestProcessorNumber.Number)
                LowestIndex = OtherIndex;
            ok_eq_ulong(FindFirstSetRightGroupAffinity(&GroupAffinity), LowestIndex);
        }
    }

    if (ActiveCount <= 16)
    {
        CombinationLimit = (KAFFINITY)1 << ActiveCount;
        for (Combination = 1; Combination < CombinationLimit; Combination++)
        {
            RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
            GroupAffinity.Mask = 0;
            LowestIndex = INVALID_PROCESSOR_INDEX;
            for (ProcessorIndex = 0; ProcessorIndex < ActiveCount; ProcessorIndex++)
            {
                if ((Combination & ((KAFFINITY)1 << ProcessorIndex)) == 0)
                    continue;

                ok_eq_hex(GetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber), STATUS_SUCCESS);
                if (LowestIndex == INVALID_PROCESSOR_INDEX)
                {
                    GroupAffinity.Group = ProcessorNumber.Group;
                    LowestIndex = ProcessorIndex;
                    LowestProcessorNumber = ProcessorNumber;
                }
                else if (ProcessorNumber.Group != GroupAffinity.Group)
                {
                    LowestIndex = INVALID_PROCESSOR_INDEX;
                    break;
                }
                GroupAffinity.Mask |= (KAFFINITY)1 << ProcessorNumber.Number;
                if (ProcessorNumber.Number < LowestProcessorNumber.Number)
                {
                    LowestIndex = ProcessorIndex;
                    LowestProcessorNumber = ProcessorNumber;
                }
            }
            if (LowestIndex != INVALID_PROCESSOR_INDEX)
                ok_eq_ulong(FindFirstSetRightGroupAffinity(&GroupAffinity), LowestIndex);
        }
    }

    ok_eq_hex(GetProcessorNumberFromIndex(0, &ProcessorNumber), STATUS_SUCCESS);
    RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
    GroupAffinity.Mask = (KAFFINITY)1 << ProcessorNumber.Number;
    GroupAffinity.Group = ProcessorNumber.Group;
    GroupSource = GroupAffinity;
    ok_eq_ulong(FindFirstSetRightGroupAffinity(&GroupAffinity), 0);
    ok_eq_size(RtlCompareMemory(&GroupAffinity, &GroupSource, sizeof(GroupAffinity)), sizeof(GroupAffinity));

    RtlInitUnicodeString(&Name, L"KeFirstGroupAffinityEx");
    FirstGroupAffinityEx = (PKMT_KE_FIRST_GROUP_AFFINITY_EX)MmGetSystemRoutineAddress(&Name);
    if (FirstGroupAffinityEx == NULL)
    {
        skip(FALSE, "KeFirstGroupAffinityEx is not exported\n");
        return;
    }

    for (GroupNumber = 0; GroupNumber <= KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
    {
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Count = GroupNumber;
        Affinity.Size = 0xA5A5;
        Affinity.Reserved = 0xA5A5A5A5;
        RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
        Source = Affinity;
        GroupSource = GroupAffinity;
        ok_eq_hex(FirstGroupAffinityEx(&GroupAffinity, &Affinity), STATUS_NOT_FOUND);
        ok_eq_size(RtlCompareMemory(&GroupAffinity, &GroupSource, sizeof(GroupAffinity)), sizeof(GroupAffinity));
        ok_eq_size(RtlCompareMemory(&Affinity, &Source, sizeof(Affinity)), sizeof(Affinity));
    }

    for (GroupNumber = 0; GroupNumber < KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
    {
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Count = GroupNumber + 1;
        Affinity.Size = 0xA5A5;
        Affinity.Reserved = 0xA5A5A5A5;
        Affinity.Bitmap[GroupNumber] = (KAFFINITY)0xA5A5A5A5A5A5A5A5ULL ^ GroupNumber;
        RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
        RtlZeroMemory(&GroupSource, sizeof(GroupSource));
        GroupSource.Mask = Affinity.Bitmap[GroupNumber];
        GroupSource.Group = GroupNumber;
        Source = Affinity;
        ok_eq_hex(FirstGroupAffinityEx(&GroupAffinity, &Affinity), STATUS_SUCCESS);
        ok_eq_size(RtlCompareMemory(&GroupAffinity, &GroupSource, sizeof(GroupAffinity)), sizeof(GroupAffinity));
        ok_eq_size(RtlCompareMemory(&Affinity, &Source, sizeof(Affinity)), sizeof(Affinity));
    }

    for (GroupNumber = 0; GroupNumber < KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
    {
        for (OtherGroup = GroupNumber + 1; OtherGroup < KAFFINITY_EX_INITIALIZED_GROUPS; OtherGroup++)
        {
            RtlZeroMemory(&Affinity, sizeof(Affinity));
            Affinity.Count = OtherGroup + 1;
            Affinity.Size = 0xA5A5;
            Affinity.Reserved = 0xA5A5A5A5;
            Affinity.Bitmap[GroupNumber] = (KAFFINITY)1 << GroupNumber;
            Affinity.Bitmap[OtherGroup] = ~(KAFFINITY)1 << OtherGroup;
            RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
            RtlZeroMemory(&GroupSource, sizeof(GroupSource));
            GroupSource.Mask = Affinity.Bitmap[GroupNumber];
            GroupSource.Group = GroupNumber;
            Source = Affinity;
            ok_eq_hex(FirstGroupAffinityEx(&GroupAffinity, &Affinity), STATUS_SUCCESS);
            ok_eq_size(RtlCompareMemory(&GroupAffinity, &GroupSource, sizeof(GroupAffinity)), sizeof(GroupAffinity));
            ok_eq_size(RtlCompareMemory(&Affinity, &Source, sizeof(Affinity)), sizeof(Affinity));
        }
    }

    for (GroupNumber = 0; GroupNumber < KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
    {
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Count = GroupNumber;
        Affinity.Size = 0xA5A5;
        Affinity.Reserved = 0xA5A5A5A5;
        Affinity.Bitmap[GroupNumber] = (KAFFINITY)1 << GroupNumber;
        RtlFillMemory(&GroupAffinity, sizeof(GroupAffinity), 0xA5);
        Source = Affinity;
        GroupSource = GroupAffinity;
        ok_eq_hex(FirstGroupAffinityEx(&GroupAffinity, &Affinity), STATUS_NOT_FOUND);
        ok_eq_size(RtlCompareMemory(&GroupAffinity, &GroupSource, sizeof(GroupAffinity)), sizeof(GroupAffinity));
        ok_eq_size(RtlCompareMemory(&Affinity, &Source, sizeof(Affinity)), sizeof(Affinity));
    }
#endif
}
