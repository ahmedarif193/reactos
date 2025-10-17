#include <precomp.h>

/*********************************************************************
 *              _strtoui64_l (MSVCRT.@)
 *
 *  Locale parameter currently ignored.
 */
unsigned __int64 CDECL _strtoui64_l(const char *nptr,
                                    char **endptr,
                                    int base,
                                    _locale_t locale)
{
    (void)locale;
    return strtoull(nptr, endptr, base);
}

/*********************************************************************
 *              _strtoui64 (MSVCRT.@)
 */
unsigned __int64 CDECL _strtoui64(const char *nptr,
                                  char **endptr,
                                  int base)
{
    return _strtoui64_l(nptr, endptr, base, NULL);
}

/* strtoull is provided by string/strtoull.c */
