/*
 * PROJECT:     GCC C++ support library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     wctob/btowc shims for GCC 15 libstdc++
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * GCC 15's libstdc++ ctype_members.o references wctype, wctob, and btowc.
 * wctype now comes from msvcrt; only wctob and btowc still need a shim here
 * for older CRT export sets.
 */

#include <stdlib.h>
#include <string.h>

#undef wctob
#undef btowc

int __cdecl wctob(unsigned int c)
{
    char buf;
    if (wctomb(&buf, (wchar_t)c) == 1)
        return (unsigned char)buf;
    return -1; /* EOF */
}

unsigned int __cdecl btowc(int c)
{
    wchar_t wc;
    unsigned char uc;

    if (c == -1)
        return (unsigned int)-1; /* WEOF */

    uc = (unsigned char)c;
    if (mbtowc(&wc, (const char *)&uc, 1) == 1)
        return (unsigned int)wc;
    return (unsigned int)-1; /* WEOF */
}

#ifdef _M_IX86
void *_imp__wctob = wctob;
void *_imp__btowc = btowc;
#else
void *__imp_wctob = wctob;
void *__imp_btowc = btowc;
#endif
