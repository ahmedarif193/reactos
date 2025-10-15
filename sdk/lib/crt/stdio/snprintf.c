/*
 * ReactOS CRT - C99 snprintf/vsnprintf shims
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Ensure we emit the C99 symbol names instead of the underscored aliases */
#undef vsnprintf
#undef snprintf

#ifdef _LIBCNT_
#define CRT_SET_ERRNO(value) ((void)0)
#else
#define CRT_SET_ERRNO(value) (errno = (value))
#endif

#ifndef va_copy
#define va_copy(dest, src) ((dest) = (src))
#endif

int __cdecl vsnprintf(char *buffer, size_t count, const char *format, va_list argptr)
{
    int length;
    int required;
    va_list copy;

    if (!buffer && count)
    {
        CRT_SET_ERRNO(EINVAL);
        return -1;
    }

    va_copy(copy, argptr);
    if (count && buffer)
    {
        length = _vsnprintf(buffer, count, format, argptr);
    }
    else
    {
        length = -1;
    }

    if (length < 0)
    {
        required = _vscprintf(format, copy);
        if (buffer && count)
        {
            buffer[count - 1] = '\0';
        }
        length = required;
    }

    va_end(copy);
    return length;
}

int __cdecl snprintf(char *buffer, size_t count, const char *format, ...)
{
    int length;
    va_list argptr;

    va_start(argptr, format);
    length = vsnprintf(buffer, count, format, argptr);
    va_end(argptr);
    return length;
}
