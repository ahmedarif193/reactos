/*
 * PROJECT:     ReactOS Universal C Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native ARM64EC memory and string search export entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winnt.h>
#include <stddef.h>

#undef RtlFillMemory
#undef RtlMoveMemory

DECLSPEC_IMPORT SIZE_T NTAPI RtlCompareMemory(const VOID *Source1, const VOID *Source2, SIZE_T Length);
DECLSPEC_IMPORT VOID NTAPI RtlFillMemory(VOID *Destination, SIZE_T Length, UCHAR Fill);
DECLSPEC_IMPORT VOID NTAPI RtlMoveMemory(VOID *Destination, const VOID *Source, SIZE_T Length);

void * __cdecl
memchr(const void *Buffer,
       int Character,
       size_t Length)
{
    const unsigned char *Current = Buffer;
    const unsigned char Value = (unsigned char)Character;

    while (Length--)
    {
        if (*Current == Value)
            return (void *)Current;

        ++Current;
    }

    return NULL;
}

int __cdecl
memcmp(const void *Buffer1,
       const void *Buffer2,
       size_t Length)
{
    const unsigned char *Left = Buffer1;
    const unsigned char *Right = Buffer2;
    SIZE_T EqualLength;

    EqualLength = RtlCompareMemory(Buffer1, Buffer2, Length);
    if (EqualLength == Length)
        return 0;

    return Left[EqualLength] - Right[EqualLength];
}

char * __cdecl
strchr(const char *String,
       int Character)
{
    const char Value = (char)Character;

    do
    {
        if (*String == Value)
            return (char *)String;
    }
    while (*String++ != '\0');

    return NULL;
}

char * __cdecl
strrchr(const char *String,
        int Character)
{
    const char Value = (char)Character;
    const char *Result = NULL;

    do
    {
        if (*String == Value)
            Result = String;
    }
    while (*String++ != '\0');

    return (char *)Result;
}

char * __cdecl
strstr(const char *String,
       const char *Substring)
{
    const char *Current;
    const char *Candidate;

    if (*Substring == '\0')
        return (char *)String;

    while (*String != '\0')
    {
        Current = String;
        Candidate = Substring;
        while (*Candidate != '\0' && *Current == *Candidate)
        {
            ++Current;
            ++Candidate;
        }

        if (*Candidate == '\0')
            return (char *)String;

        ++String;
    }

    return NULL;
}

wchar_t * __cdecl
wcschr(const wchar_t *String,
       wchar_t Character)
{
    do
    {
        if (*String == Character)
            return (wchar_t *)String;
    }
    while (*String++ != L'\0');

    return NULL;
}

wchar_t * __cdecl
wcsrchr(const wchar_t *String,
        wchar_t Character)
{
    const wchar_t *Result = NULL;

    do
    {
        if (*String == Character)
            Result = String;
    }
    while (*String++ != L'\0');

    return (wchar_t *)Result;
}

wchar_t * __cdecl
wcsstr(const wchar_t *String,
       const wchar_t *Substring)
{
    const wchar_t *Current;
    const wchar_t *Candidate;

    if (*Substring == L'\0')
        return (wchar_t *)String;

    while (*String != L'\0')
    {
        Current = String;
        Candidate = Substring;
        while (*Candidate != L'\0' && *Current == *Candidate)
        {
            ++Current;
            ++Candidate;
        }

        if (*Candidate == L'\0')
            return (wchar_t *)String;

        ++String;
    }

    return NULL;
}

void * __cdecl
memcpy(void *Destination,
       const void *Source,
       size_t Length)
{
    RtlMoveMemory(Destination, Source, Length);
    return Destination;
}

void * __cdecl
memmove(void *Destination,
        const void *Source,
        size_t Length)
{
    RtlMoveMemory(Destination, Source, Length);
    return Destination;
}

void * __cdecl
memset(void *Destination,
       int Value,
       size_t Length)
{
    RtlFillMemory(Destination, Length, (UCHAR)Value);
    return Destination;
}
