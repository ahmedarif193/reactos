/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER within this package.
 */
#ifndef _INC_SETJMPEX
#define _INC_SETJMPEX

#ifndef _WIN32
#error Only Win32 target is supported!
#endif

#if (defined(_X86_) && !defined(__x86_64))
  __declspec(noreturn) __MINGW_NOTHROW void __cdecl _longjmpex(jmp_buf _Buf,int _Value);
#define setjmp _setjmp
#define longjmp _longjmpex
#else
#ifdef setjmp
#undef setjmp
#endif
#if defined(_M_ARM64EC) || defined(__arm64ec__)
/*
 * Clang supplies the hidden frame argument only for its _setjmp builtin on
 * ARM64EC.  Both Win64 entry points use the same frame-aware implementation,
 * so retain the extended semantics while routing through that builtin.
 */
#define setjmp _setjmp
#define setjmpex _setjmp
#else
#define setjmp _setjmpex
#endif
#endif

#include <setjmp.h>
#endif
