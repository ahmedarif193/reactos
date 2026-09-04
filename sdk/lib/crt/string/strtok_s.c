/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Reentrant string tokenizer
 * COPYRIGHT:   Copyright 2026 ReactOS Project
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
