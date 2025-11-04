/*
 * Provide __acrt_iob_func import symbols when building 32-bit ReactOS
 * binaries with a multilib GCC toolchain. GCC's libsupc++ expects the
 * function to be exported by msvcrt, but our multilib import library
 * intentionally omits startup objects, so we recreate the entry here.
 */

#if defined(__GNUC__) && defined(__i386__)

#include <stdio.h>

static FILE *__acrt_iob_entry(unsigned int index)
{
    return &__iob_func()[index];
}

FILE *__cdecl __acrt_iob_func(unsigned int index)
{
    return __acrt_iob_entry(index);
}

#if defined(_WIN64)
const void * __imp___acrt_iob_func = __acrt_iob_func;
#else
const void * _imp____acrt_iob_func = __acrt_iob_func;
#endif

#endif /* __GNUC__ */
