/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite MmSecureVirtualMemory API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(MmSecureKM)
{
    PVOID BaseAddress = NULL;
    SIZE_T RegionSize = PAGE_SIZE * 2;
    HANDLE Secure;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &BaseAddress, 0, &RegionSize, MEM_COMMIT, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Secure = MmSecureVirtualMemory(BaseAddress, PAGE_SIZE, PAGE_READWRITE);
    ok(Secure != NULL, "MmSecureVirtualMemory failed\n");
    if (Secure != NULL)
        MmUnsecureVirtualMemory(Secure);

    Secure = MmSecureVirtualMemory(BaseAddress, PAGE_SIZE, PAGE_READONLY);
    ok(Secure != NULL, "secure readonly on rw failed\n");
    if (Secure != NULL)
        MmUnsecureVirtualMemory(Secure);

    Status = ZwFreeVirtualMemory(ZwCurrentProcess(), &BaseAddress, &RegionSize, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
