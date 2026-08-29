/*
 * PROJECT:     GCC C++ support library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Assertion import shim for GCC 16 libmingwex
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * GCC 16's libmingwex _assert wrapper imports the internal UCRT function
 * __msvcrt_assert. ReactOS exports the compatible _assert entry point instead,
 * so redirect the import pointer to it.
 */

void __cdecl _assert(const char *, const char *, unsigned int);

#ifdef _M_IX86
void *_imp____msvcrt_assert = _assert;
#else
void *__imp___msvcrt_assert = _assert;
#endif
