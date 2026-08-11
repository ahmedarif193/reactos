/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 extended-affinity ABI and core operation tests
 */

#include <kmt_test.h>

VOID Test_KeArm64AffinityEx(VOID);

#ifdef _M_ARM64
typedef SIZE_T (NTAPI *PKMT_KE_SIZE_OF_AFFINITY_EX)(_In_ USHORT Count);
#endif

START_TEST(KeArm64AffinityEx)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64AffinityEx is ARM64-only\n");
#else
    KAFFINITY_EX Affinity;
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
#endif
}
