/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reentrant string tokenizer
 * COPYRIGHT:   Copyright 2026 LiberNT Project
 */

#include <string.h>

char * __cdecl strtok_s(char *str, const char *delim, char **context)
{
    if (delim == NULL || context == NULL || (str == NULL && *context == NULL))
        return NULL;

    if (str == NULL)
        str = *context;

    while (*str != '\0' && strchr(delim, *str) != NULL)
        str++;

    if (*str == '\0')
    {
        *context = str;
        return NULL;
    }

    *context = str + 1;
    while (**context != '\0' && strchr(delim, **context) == NULL)
        (*context)++;

    if (**context != '\0')
        *(*context)++ = '\0';

    return str;
}
