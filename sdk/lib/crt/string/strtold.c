#include <precomp.h>

/*
 * @implemented
 */
long double
strtold(const char *nptr,
        char **endptr)
{
    double value = strtod(nptr, endptr);
    return (long double)value;
}

/*
 * MinGW helper used by libmingwex; behaves like strtold with the
 * Windows long-double (64-bit) semantics enforced by the toolchain flags.
 */
long double
__mingw_strtold(const char *nptr,
                char **endptr)
{
    return strtold(nptr, endptr);
}
