#include "precomp.h"

#include <wchar.h>
#include <errno.h>

int __cdecl wctob(wint_t wc)
{
    if (wc == WEOF)
        return EOF;

    unsigned char c = (unsigned char)wc;
    return (wc == (wint_t)c) ? (int)c : EOF;
}
