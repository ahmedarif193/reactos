/***
*wcslen.c / wcsnlen.c - portable ARM64 implementations of wide-character length helpers
*
*      Replaces the original MASM sources that were incompatible with GCC.
*
*******************************************************************************/

#include <stddef.h>
#include <wchar.h>

size_t __cdecl wcslen(const wchar_t* string)
{
    wchar_t const* current = string;

    while (*current != L'\0')
    {
        ++current;
    }

    return (size_t)(current - string);
}

size_t __cdecl wcsnlen(const wchar_t* string, size_t maximum_count)
{
    size_t index = 0;

    while (index < maximum_count && string[index] != L'\0')
    {
        ++index;
    }

    return index;
}
