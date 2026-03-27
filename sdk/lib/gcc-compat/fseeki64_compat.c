/*
 * PROJECT:     GCC C++ support library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     _fseeki64 shim for GCC 15 libstdc++
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * GCC 15's libstdc++ ext11-inst.o references _fseeki64 as a DLL import.
 * _fseeki64 is version-gated to >= 0x600 in msvcrt. For older versions,
 * delegate to fseek (32-bit offset).
 * _ftelli64 is handled unconditionally in ftelli64_compat.c.
 */

typedef struct _iobuf FILE;
int __cdecl fseek(FILE *, long, int);

int __cdecl _fseeki64(FILE *stream, long long offset, int origin)
{
    return fseek(stream, (long)offset, origin);
}

#ifdef _M_IX86
void *_imp___fseeki64 = _fseeki64;
#else
void *__imp__fseeki64 = _fseeki64;
#endif
