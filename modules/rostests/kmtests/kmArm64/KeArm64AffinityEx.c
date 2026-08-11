/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 extended-affinity ABI and core operation tests
 */

#include <kmt_test.h>

VOID Test_KeArm64AffinityEx(VOID);

#ifdef _M_ARM64
typedef LOGICAL (NTAPI *PKMT_KE_AND_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2, _Out_opt_ PKAFFINITY_EX Result);
typedef VOID (NTAPI *PKMT_KE_COPY_AFFINITY_EX)(_Out_ PKAFFINITY_EX Destination, _In_ PKAFFINITY_EX Source);
typedef SIZE_T (NTAPI *PKMT_KE_SIZE_OF_AFFINITY_EX)(_In_ USHORT Count);
typedef ULONG (NTAPI *PKMT_KE_COUNT_SET_BITS_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity);
typedef LOGICAL (NTAPI *PKMT_KE_IS_EQUAL_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2);
typedef LOGICAL (NTAPI *PKMT_KE_IS_SINGLE_GROUP_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity, _Out_opt_ PUSHORT Group);
typedef LOGICAL (NTAPI *PKMT_KE_IS_SUBSET_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2);
#endif

START_TEST(KeArm64AffinityEx)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64AffinityEx is ARM64-only\n");
#else
    KAFFINITY_EX Affinity;
    KAFFINITY_EX Result;
    KAFFINITY_EX Source;
    PKMT_KE_AND_AFFINITY_EX AndAffinityEx;
    PKMT_KE_COPY_AFFINITY_EX CopyAffinityEx;
    PKMT_KE_COUNT_SET_BITS_AFFINITY_EX CountSetBitsAffinityEx;
    PKMT_KE_IS_EQUAL_AFFINITY_EX IsEqualAffinityEx;
    PKMT_KE_IS_SINGLE_GROUP_AFFINITY_EX IsSingleGroupAffinityEx;
    PKMT_KE_IS_SUBSET_AFFINITY_EX IsSubsetAffinityEx;
    PKMT_KE_SIZE_OF_AFFINITY_EX SizeOfAffinityEx;
    UNICODE_STRING Name;
    USHORT GroupNumber;

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
#endif
}
