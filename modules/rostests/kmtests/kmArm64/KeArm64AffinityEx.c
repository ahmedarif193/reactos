/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 extended-affinity ABI and core operation tests
 */

#include <kmt_test.h>

VOID Test_KeArm64AffinityEx(VOID);

#ifdef _M_ARM64
typedef VOID (NTAPI *PKMT_KE_COPY_AFFINITY_EX)(_Out_ PKAFFINITY_EX Destination, _In_ PKAFFINITY_EX Source);
typedef SIZE_T (NTAPI *PKMT_KE_SIZE_OF_AFFINITY_EX)(_In_ USHORT Count);
typedef ULONG (NTAPI *PKMT_KE_COUNT_SET_BITS_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity);
typedef LOGICAL (NTAPI *PKMT_KE_IS_EQUAL_AFFINITY_EX)(_In_ PKAFFINITY_EX Affinity1, _In_ PKAFFINITY_EX Affinity2);
#endif

START_TEST(KeArm64AffinityEx)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64AffinityEx is ARM64-only\n");
#else
    KAFFINITY_EX Affinity;
    KAFFINITY_EX Source;
    PKMT_KE_COPY_AFFINITY_EX CopyAffinityEx;
    PKMT_KE_COUNT_SET_BITS_AFFINITY_EX CountSetBitsAffinityEx;
    PKMT_KE_IS_EQUAL_AFFINITY_EX IsEqualAffinityEx;
    PKMT_KE_SIZE_OF_AFFINITY_EX SizeOfAffinityEx;
    UNICODE_STRING Name;

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
#endif
}
