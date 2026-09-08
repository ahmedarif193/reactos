/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    const WCHAR *texts[] = {L"", L"HandleDispatcherRequestProcessingFailure", L"Alpha", L"a\x0301", L"\x0130\x0131", L"a-b", L"\x65e5\x672c"};
    DWORD flags[] = {LCMAP_SORTKEY, LCMAP_SORTKEY | NORM_IGNORECASE, LCMAP_SORTKEY | NORM_IGNORECASE | NORM_LINGUISTIC_CASING};
    unsigned i, j, cycle, failures = 0;
    BYTE buffer[1024];
    setvbuf(stdout, NULL, _IONBF, 0);
    for (cycle = 0; cycle < 1000; ++cycle)
    for (i = 0; i < sizeof(texts)/sizeof(texts[0]); ++i)
    for (j = 0; j < sizeof(flags)/sizeof(flags[0]); ++j)
    {
        int length, actual, shortResult;
        DWORD error;
        length = LCMapStringEx(L"", flags[j], texts[i], -1, NULL, 0, NULL, NULL, 0);
        if (length <= 0 || length >= sizeof(buffer)) { failures++; continue; }
        memset(buffer, 0xcc, sizeof(buffer));
        actual = LCMapStringEx(L"", flags[j], texts[i], -1, (WCHAR *)buffer, length, NULL, NULL, 0);
        if (actual != length || buffer[length-1] != 0 || buffer[length] != 0xcc) failures++;
        SetLastError(0);
        shortResult = LCMapStringEx(L"", flags[j], texts[i], -1, (WCHAR *)buffer, length - 1, NULL, NULL, 0);
        error = GetLastError();
        if (shortResult != 0 || error != ERROR_INSUFFICIENT_BUFFER) failures++;
        if (!cycle) printf("NLS_SORTKEY text=%u flags=%lx size=%d actual=%d short=%d error=%lu\n", i, flags[j], length, actual, shortResult, error);
    }
    printf("NLS_SORTKEY_DONE cases=21000 failures=%u\n", failures);
    return failures ? 1 : 0;
}
