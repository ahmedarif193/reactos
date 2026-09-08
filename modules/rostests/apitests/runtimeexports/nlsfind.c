/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
typedef int (WINAPI *FIND)(LPCWSTR,DWORD,LPCWSTR,int,LPCWSTR,int,int*,LPNLSVERSIONINFO,void*,LPARAM);
int main(void)
{
    FIND find = (FIND)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FindNLSStringEx");
    DWORD directions[] = {FIND_FROMSTART, FIND_FROMEND, FIND_STARTSWITH, FIND_ENDSWITH};
    int expected[] = {2, 7, -1, 7};
    unsigned i, cycle, failures = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!find) return 1;
    for (cycle = 0; cycle < 5000; ++cycle) for (i = 0; i < 4; ++i)
    {
        int length = -123, result;
        SetLastError(0x1234);
        result = find(L"en-US", directions[i] | NORM_IGNORECASE | NORM_LINGUISTIC_CASING, L"xxAbcYYabc", 10, L"abc", 3, &length, NULL, NULL, 0);
        if (result != expected[i] || (result >= 0 && length != 3)) failures++;
        if (!cycle) printf("NLS_FIND direction=%lx result=%d length=%d error=%lu\n", directions[i], result, length, GetLastError());
    }
    printf("NLS_FIND_DONE cases=20000 failures=%u\n", failures);
    return failures ? 1 : 0;
}
