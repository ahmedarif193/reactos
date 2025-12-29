/***
*strlen.c / strnlen.c - portable ARM64 implementations of strlen/strnlen
*
*      Replaces the original MASM sources that were incompatible with GCC.
*
*******************************************************************************/

#include <stddef.h>
#include <string.h>

size_t __cdecl strlen(const char* string)
{
    char const* current = string;

    while (*current != '\0')
    {
        ++current;
    }

    return (size_t)(current - string);
}

size_t __cdecl strnlen(const char* string, size_t maximum_count)
{
    size_t index = 0;

    while (index < maximum_count && string[index] != '\0')
    {
        ++index;
    }

    return index;
}
