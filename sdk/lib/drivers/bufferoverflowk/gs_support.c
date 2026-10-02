/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stack cookie support provided to drivers by BufferOverflowFastFailK.lib
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define DEFAULT_SECURITY_COOKIE ((ULONG_PTR)0x00002B992DDFA232ULL)

ULONG_PTR __security_cookie = DEFAULT_SECURITY_COOKIE;
ULONG_PTR __security_cookie_complement = ~DEFAULT_SECURITY_COOKIE;

VOID
__cdecl
__security_init_cookie(VOID)
{
    LARGE_INTEGER Counter;
    ULONG_PTR Cookie;

    if (__security_cookie != DEFAULT_SECURITY_COOKIE && __security_cookie != 0)
    {
        return;
    }

    Counter = KeQueryPerformanceCounter(NULL);
    Cookie = (ULONG_PTR)Counter.QuadPart ^ (ULONG_PTR)KeQueryInterruptTime() ^ (ULONG_PTR)&__security_cookie;
    if (sizeof(ULONG_PTR) == sizeof(ULONG64))
    {
        Cookie &= (ULONG_PTR)0x0000FFFFFFFFFFFFULL;
    }
    if (Cookie == DEFAULT_SECURITY_COOKIE || Cookie == 0)
    {
        Cookie = DEFAULT_SECURITY_COOKIE + 1;
    }

    __security_cookie = Cookie;
    __security_cookie_complement = ~Cookie;
}

VOID
__cdecl
__security_check_cookie(
    _In_ ULONG_PTR Cookie)
{
    if (Cookie != __security_cookie)
    {
        KeBugCheckEx(DRIVER_OVERRAN_STACK_BUFFER, Cookie, __security_cookie, __security_cookie_complement, 0);
    }
}
