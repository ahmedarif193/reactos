/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <apitest.h>
START_TEST(localeparent)
{
    const WCHAR *names[] = {L"en", L"fr", L"de", L"it"};
    WCHAR buffer[32];
    unsigned i, cycle, failures = 0;
    int result;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (cycle = 0; cycle < 10; cycle++) for (i = 0; i < 4; i++)
    {
        result = GetLocaleInfoEx(names[i], LOCALE_SNAME | LOCALE_NOUSEROVERRIDE, buffer, 32);
        if (result != 3 || wcscmp(buffer, names[i])) failures++;
        result = GetLocaleInfoEx(names[i], LOCALE_SPARENT | LOCALE_NOUSEROVERRIDE, buffer, 32);
        if (result != 1 || buffer[0]) failures++;
        result = GetLocaleInfoEx(names[i], LOCALE_SPARENT | LOCALE_NOUSEROVERRIDE, NULL, 0);
        if (result != 1) failures++;
    }
    printf("LOCALE_PARENT_DONE cases=120 failures=%u\n", failures);
    ok(failures == 0, "%u failures in 120 cases\n", failures);
}
