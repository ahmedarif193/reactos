/*
 * ReactOS CRT - wctype implementation
 */

#include <ctype.h>
#include <string.h>
#include <wctype.h>

typedef struct
{
    const char *name;
    wctype_t    mask;
} wctype_entry;

static const wctype_entry wctype_map[] =
{
    { "alnum",   _ALPHA | _DIGIT },
    { "alpha",   _ALPHA },
    { "blank",   _BLANK },
    { "cntrl",   _CONTROL },
    { "digit",   _DIGIT },
    { "graph",   _PUNCT | _ALPHA | _DIGIT },
    { "lower",   _LOWER },
    { "print",   _BLANK | _PUNCT | _ALPHA | _DIGIT },
    { "punct",   _PUNCT },
    { "space",   _SPACE },
    { "upper",   _UPPER },
    { "xdigit",  _HEX },
    { "leadbyte", _LEADBYTE }
};

wctype_t __cdecl wctype(const char *property)
{
    size_t i;

    if (!property)
    {
        return 0;
    }

    for (i = 0; i < sizeof(wctype_map) / sizeof(wctype_map[0]); ++i)
    {
        if (_stricmp(property, wctype_map[i].name) == 0)
        {
            return wctype_map[i].mask;
        }
    }

    return 0;
}

