/*
 * PROJECT:     ReactOS Raspberry Pi 5 graphics validation
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     C++17 compatibility boundary for the unmodified glmark2 sources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#if defined(__cplusplus) && __cplusplus >= 201103L
#if !defined(_GCC_MAX_ALIGN_T) && !defined(__CLANG_MAX_ALIGN_T_DEFINED)
#define _GCC_MAX_ALIGN_T
#define __CLANG_MAX_ALIGN_T_DEFINED
typedef struct
{
    long long LongLong __attribute__((__aligned__(__alignof__(long long))));
    long double LongDouble __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;
#endif
#endif

/*
 * ReactOS's math.h declares these functions without a nothrow attribute,
 * while its legacy float.h adds one.  Load both once with matching
 * declarations before libc++ reaches float.h through its C wrappers.
 */
#include <math.h>
#undef __MINGW_NOTHROW
#define __MINGW_NOTHROW
#include <float.h>
