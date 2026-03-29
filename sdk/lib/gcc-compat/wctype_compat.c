/*
 * Provide wctype() for libstdc++ compatibility.
 * MSVCRT does not export wctype(), only iswctype() and the _wctype table.
 *
 * For pre-0x600 CRTs, wchar_compat.c provides wctype together with wctob/btowc.
 * Keep this file in the target so incremental static-library rebuilds overwrite
 * any stale archive member, but emit no symbol in that configuration.
 */

#include <wctype.h>
#include <string.h>

#if DLL_EXPORT_VERSION >= 0x600

wctype_t wctype(const char *property)
{
    static const struct { const char *name; wctype_t mask; } classes[] = {
        { "alnum",  _ALPHA | _DIGIT },
        { "alpha",  _ALPHA },
        { "blank",  _BLANK },
        { "cntrl",  _CONTROL },
        { "digit",  _DIGIT },
        { "graph",  _ALPHA | _DIGIT | _PUNCT },
        { "lower",  _LOWER },
        { "print",  _ALPHA | _DIGIT | _PUNCT | _BLANK },
        { "punct",  _PUNCT },
        { "space",  _SPACE },
        { "upper",  _UPPER },
        { "xdigit", _HEX },
    };

    for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]); i++)
    {
        if (strcmp(property, classes[i].name) == 0)
            return classes[i].mask;
    }
    return 0;
}

#endif
