/*
 * PROJECT:     GCC c++ support library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     strnlen implementation
 */

#include <stddef.h>

size_t
__cdecl
strnlen(const char *string, size_t maxlen)
{
    size_t i;

    for (i = 0; i < maxlen; ++i)
    {
        if (string[i] == '\0')
            break;
    }

    return i;
}
