/*
 * PROJECT:     GCC C++ support library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     64-bit file offset shims for GCC 16 libstdc++
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * GCC 16's libstdc++ uses the mingw-w64 POSIX-style large-file wrappers.
 * Redirect them to the equivalent MSVCRT entry points exported by ReactOS.
 */

typedef struct _iobuf FILE;

long long __cdecl _lseeki64(int, long long, int);
int __cdecl _fstat64(int, void *);
int __cdecl _fseeki64(FILE *, long long, int);
long long __cdecl _ftelli64(FILE *);
int __cdecl _chsize_s(int, long long);

long long __cdecl lseek64(int descriptor, long long offset, int origin)
{
    return _lseeki64(descriptor, offset, origin);
}

int __cdecl fstat64(int descriptor, void *status)
{
    return _fstat64(descriptor, status);
}

int __cdecl ftruncate64(int descriptor, long long length)
{
    return _chsize_s(descriptor, length);
}

int __cdecl fseeko64(FILE *stream, long long offset, int origin)
{
    return _fseeki64(stream, offset, origin);
}

long long __cdecl ftello64(FILE *stream)
{
    return _ftelli64(stream);
}

#ifdef _M_IX86
void *_imp__fseeko64 = fseeko64;
void *_imp__ftello64 = ftello64;
#else
void *__imp_fseeko64 = fseeko64;
void *__imp_ftello64 = ftello64;
#endif
