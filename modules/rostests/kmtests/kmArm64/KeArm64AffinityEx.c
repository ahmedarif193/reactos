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
} KMT_AFFINITY_EX2_BUFFER;

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

typedef LOGICAL (NTAPI *PKMT_KE_AND_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Out_opt_ PKAFFINITY_EX Result);
typedef VOID (NTAPI *PKMT_KE_COPY_AFFINITY_EX)(_Out_ PKAFFINITY_EX Destination, _In_ PKAFFINITY_EX Source);
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
typedef SIZE_T (NTAPI *PKMT_KE_SIZE_OF_AFFINITY_EX)(_In_ USHORT Count);
typedef ULONG (NTAPI *PKMT_KE_GET_PROCESSOR_INDEX_FROM_NUMBER)(_In_ PPROCESSOR_NUMBER ProcessorNumber);
typedef NTSTATUS (NTAPI *PKMT_KE_GET_PROCESSOR_NUMBER_FROM_INDEX)(_In_ ULONG ProcessorIndex, _Out_ PPROCESSOR_NUMBER ProcessorNumber);
typedef ULONG (NTAPI *PKMT_KE_COUNT_SET_BITS_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity);
typedef LOGICAL (NTAPI *PKMT_KE_IS_EQUAL_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2);
typedef LOGICAL (NTAPI *PKMT_KE_IS_SINGLE_GROUP_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity, _Out_opt_ PUSHORT Group);
typedef LOGICAL (NTAPI *PKMT_KE_IS_SUBSET_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2);
typedef LOGICAL (NTAPI *PKMT_KE_OR_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Out_opt_ PKAFFINITY_EX Result);
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
    KMT_AFFINITY_ENUMERATION_CONTEXT_BUFFER EnumerationBuffer;
    KMT_AFFINITY_ENUMERATION_CONTEXT_BUFFER EnumerationSource;
    KMT_GROUP_AFFINITY_BUFFER GroupBuffer;
    KMT_GROUP_AFFINITY_BUFFER GroupBufferSource;
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
    PKMT_KE_COPY_AFFINITY_EX CopyAffinityEx;
    PKMT_KE_COUNT_SET_BITS_AFFINITY_EX CountSetBitsAffinityEx;
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
    PKMT_KE_GET_PROCESSOR_INDEX_FROM_NUMBER GetProcessorIndexFromNumber;
    PKMT_KE_GET_PROCESSOR_NUMBER_FROM_INDEX GetProcessorNumberFromIndex;
    PKMT_KE_IS_EQUAL_AFFINITY_EX IsEqualAffinityEx;
    PKMT_KE_IS_SINGLE_GROUP_AFFINITY_EX IsSingleGroupAffinityEx;
    PKMT_KE_IS_SUBSET_AFFINITY_EX IsSubsetAffinityEx;
    PKMT_KE_OR_AFFINITY_EX OrAffinityEx;
    PKMT_KE_SIZE_OF_AFFINITY_EX SizeOfAffinityEx;
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
    ULONG CountIndex;
    ULONG MaskIndex;
    ULONG GroupValue;

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
