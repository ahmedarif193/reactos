/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite Ex time conversion API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(ExTimeZone)
{
    LARGE_INTEGER SystemTime, LocalTime, RoundTrip;

    KeQuerySystemTime(&SystemTime);

    ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
    ExLocalTimeToSystemTime(&LocalTime, &RoundTrip);
    ok_eq_longlong(RoundTrip.QuadPart, SystemTime.QuadPart);

    ok((LocalTime.QuadPart - SystemTime.QuadPart) % (10000000LL * 60 * 15) == 0,
       "timezone bias not a multiple of 15 minutes: %I64d\n",
       LocalTime.QuadPart - SystemTime.QuadPart);

    ok(LocalTime.QuadPart - SystemTime.QuadPart <= 14LL * 3600 * 10000000 &&
       SystemTime.QuadPart - LocalTime.QuadPart <= 14LL * 3600 * 10000000,
       "timezone bias exceeds 14h: %I64d\n",
       LocalTime.QuadPart - SystemTime.QuadPart);
}
