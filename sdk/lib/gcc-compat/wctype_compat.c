/*
 * Provide wctype() for libstdc++ compatibility.
 * MSVCRT does not export wctype(), only iswctype() and the _wctype table.
 */

#include <wctype.h>
#include <string.h>

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
