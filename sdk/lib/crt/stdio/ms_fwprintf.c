/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     mingw-w64 __ms_fwprintf glue function
 */

/*
 * The mingw-w64 wide printf engine (mingw_wpformat.c, part of libmingwex)
 * references __ms_fwprintf. Unlike the sibling __ms_* stdio wrappers, which are
 * ASM_CALL aliases redirected onto CRT exports, __ms_fwprintf has no such alias;
 * mingw-w64 expects it to be provided as a real function by the CRT import
 * libraries. As ReactOS ships its own CRT, provide it here so usermode modules
 * that pull in the mingw wide-format helpers link cleanly.
 */

#ifdef __GNUC__

#include <precomp.h>
#include <stdarg.h>

int CDECL __ms_fwprintf(FILE *file, const wchar_t *format, ...)
{
    va_list argptr;
    int result;

    va_start(argptr, format);
    result = vfwprintf(file, format, argptr);
    va_end(argptr);

    return result;
}

#endif /* __GNUC__ */
