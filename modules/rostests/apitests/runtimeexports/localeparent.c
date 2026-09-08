/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
int main(void)
{
    const WCHAR *names[] = {L"en", L"fr", L"de", L"it"};
    WCHAR buffer[32];
    unsigned i, cycle, failures = 0;
    int result;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (cycle = 0; cycle < 1000; cycle++) for (i = 0; i < 4; i++)
    {
        result = GetLocaleInfoEx(names[i], LOCALE_SNAME | LOCALE_NOUSEROVERRIDE, buffer, 32);
        if (result != 3 || wcscmp(buffer, names[i])) failures++;
        result = GetLocaleInfoEx(names[i], LOCALE_SPARENT | LOCALE_NOUSEROVERRIDE, buffer, 32);
        if (result != 1 || buffer[0]) failures++;
        result = GetLocaleInfoEx(names[i], LOCALE_SPARENT | LOCALE_NOUSEROVERRIDE, NULL, 0);
        if (result != 1) failures++;
    }
    printf("LOCALE_PARENT_DONE cases=12000 failures=%u\n", failures);
    return failures ? 1 : 0;
}
