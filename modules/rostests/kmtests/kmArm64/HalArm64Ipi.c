/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 HAL IPI contract tests
 */

#include <kmt_test.h>

VOID Test_HalArm64Ipi(VOID);

#ifdef _M_ARM64

#define KMT_ARM64_IPI_VECTOR 0xE01
#define KMT_MAXIMUM_GROUPS   32

typedef enum _KMT_IPI_TYPE
{
    KmtIpiAffinity = 0,
    KmtIpiAllButSelf,
    KmtIpiAll
} KMT_IPI_TYPE;

typedef struct _KMT_AFFINITY_EX
{
    USHORT Count;
    USHORT Size;
    ULONG Reserved;
    KAFFINITY Bitmap[KMT_MAXIMUM_GROUPS];
} KMT_AFFINITY_EX, *PKMT_AFFINITY_EX;

typedef NTSTATUS (NTAPI *PKMT_HAL_REQUEST_IPI_SPECIFY_VECTOR)(KMT_IPI_TYPE IpiType, PKMT_AFFINITY_EX Affinity, ULONG Vector);

#endif

START_TEST(HalArm64Ipi)
{
#ifndef _M_ARM64
    skip(FALSE, "HalArm64Ipi is ARM64-only\n");
#else
    PKMT_HAL_REQUEST_IPI_SPECIFY_VECTOR RequestIpi;
    KMT_AFFINITY_EX Affinity;
    UNICODE_STRING Name;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"HalRequestIpiSpecifyVector");
    RequestIpi = (PKMT_HAL_REQUEST_IPI_SPECIFY_VECTOR)MmGetSystemRoutineAddress(&Name);
    if (RequestIpi == NULL)
    {
        skip(FALSE, "HalRequestIpiSpecifyVector is not exported\n");
        return;
    }

    Status = RequestIpi((KMT_IPI_TYPE)3, NULL, KMT_ARM64_IPI_VECTOR);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = RequestIpi(KmtIpiAffinity, NULL, KMT_ARM64_IPI_VECTOR);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Count = 1;
    Affinity.Size = KMT_MAXIMUM_GROUPS;
    Status = RequestIpi(KmtIpiAffinity, &Affinity, KMT_ARM64_IPI_VECTOR);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Affinity.Bitmap[0] = (KAFFINITY)1 << KeGetCurrentProcessorNumber();
    Status = RequestIpi(KmtIpiAffinity, &Affinity, KMT_ARM64_IPI_VECTOR);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = RequestIpi(KmtIpiAllButSelf, NULL, KMT_ARM64_IPI_VECTOR);
    ok_eq_hex(Status, STATUS_SUCCESS);
#endif
}
