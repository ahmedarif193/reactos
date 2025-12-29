#include <wchar.h>
#include <stddef.h>

size_t __cdecl wcslen(const wchar_t *str)
{
    const wchar_t *s = str;
    while (*s)
        ++s;
    return (size_t)(s - str);
}
