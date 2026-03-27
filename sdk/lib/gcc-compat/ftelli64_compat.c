/*
 * PROJECT:     GCC C++ support library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     _ftelli64 shim for GCC 15 libstdc++
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * GCC 15's libstdc++ ext11-inst.o references _ftelli64 as a DLL import.
 * msvcrt does not export _ftelli64 at any version, so we provide it here.
 */

typedef struct _iobuf FILE;
long __cdecl ftell(FILE *);

long long __cdecl _ftelli64(FILE *stream)
{
    return (long long)ftell(stream);
}

#ifdef _M_IX86
void *_imp___ftelli64 = _ftelli64;
#else
void *__imp__ftelli64 = _ftelli64;
#endif
