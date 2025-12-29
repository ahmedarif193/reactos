#include <crtdefs.h>

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

static void __reactos_fastfail_impl(unsigned int code)
{
    (void)code;
    __builtin_trap();
}

void __cdecl __fastfail(unsigned int code) __attribute__((alias("__reactos_fastfail_impl")));
