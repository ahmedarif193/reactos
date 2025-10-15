/*
 * ReactOS CRT - btowc and wctob implementations
 */

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>

wint_t __cdecl btowc(int c)
{
    unsigned char ch;
    wchar_t wc;
    mbstate_t state;
    size_t result;

    if (c == EOF)
    {
        return WEOF;
    }

    if ((unsigned int)c > 0xff)
    {
        return WEOF;
    }

    ch = (unsigned char)c;
    memset(&state, 0, sizeof(state));
    result = mbrtowc(&wc, (const char *)&ch, 1, &state);

    if (result == (size_t)-1 || result == (size_t)-2)
    {
        errno = EILSEQ;
        return WEOF;
    }

    return wc;
}

int __cdecl wctob(wint_t wc)
{
    char buf[MB_LEN_MAX];
    mbstate_t state;
    size_t result;

    if (wc == WEOF)
    {
        return EOF;
    }

    memset(&state, 0, sizeof(state));
    result = wcrtomb(buf, wc, &state);

    if (result == (size_t)-1)
    {
        errno = EILSEQ;
        return EOF;
    }

    if (result != 1)
    {
        return EOF;
    }

    return (unsigned char)buf[0];
}
