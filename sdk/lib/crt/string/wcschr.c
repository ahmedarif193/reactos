#include <wchar.h>

wchar_t * __cdecl wcschr(const wchar_t *s, wchar_t c)
{
    while (*s)
    {
        if (*s == c)
            return (wchar_t *)s;
        ++s;
    }
    return (c == 0) ? (wchar_t *)s : NULL;
}
