/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     RtlGetVersion contract test
 *
 * RtlGetVersion is exported by ntdll/ntoskrnl since NT4 and is the
 * canonical way to query the kernel-reported OS version.
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(RtlGetVersion)
{
    RTL_OSVERSIONINFOEXW Info;
    NTSTATUS Status;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.dwOSVersionInfoSize = sizeof(Info);

    Status = RtlGetVersion((PRTL_OSVERSIONINFOW)&Info);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "RtlGetVersion failed\n"))
        return;

    trace("RtlGetVersion: %lu.%lu build %lu SP %u.%u, ProductType=%u, "
          "PlatformId=%lu, CSD='%ls'\n",
          Info.dwMajorVersion, Info.dwMinorVersion, Info.dwBuildNumber,
          Info.wServicePackMajor, Info.wServicePackMinor,
          Info.wProductType, Info.dwPlatformId, Info.szCSDVersion);

    ok_eq_ulong(Info.dwPlatformId, (ULONG)VER_PLATFORM_WIN32_NT);
    ok_eq_ulong(Info.dwMajorVersion, (ULONG)(GetNTVersion() >> 8));
    ok_eq_ulong(Info.dwMinorVersion, (ULONG)(GetNTVersion() & 0xFF));
    ok(Info.dwBuildNumber != 0, "dwBuildNumber is zero\n");
    ok(Info.wProductType == VER_NT_WORKSTATION ||
       Info.wProductType == VER_NT_DOMAIN_CONTROLLER ||
       Info.wProductType == VER_NT_SERVER,
       "wProductType = %u, expected 1/2/3\n", Info.wProductType);

    /* Verify that querying with the smaller RTL_OSVERSIONINFOW size also
     * succeeds and returns the same major/minor/build values. */
    {
        RTL_OSVERSIONINFOW Small;
        RtlZeroMemory(&Small, sizeof(Small));
        Small.dwOSVersionInfoSize = sizeof(Small);
        Status = RtlGetVersion(&Small);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Small.dwMajorVersion, Info.dwMajorVersion);
        ok_eq_ulong(Small.dwMinorVersion, Info.dwMinorVersion);
        ok_eq_ulong(Small.dwBuildNumber, Info.dwBuildNumber);
        ok_eq_ulong(Small.dwPlatformId, Info.dwPlatformId);
    }
}
