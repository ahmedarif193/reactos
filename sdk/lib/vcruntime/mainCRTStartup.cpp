//
// mainCRTStartup.c
//
//      Copyright (c) 2024 Timo Kreuzer
//
// Implementation of ANSI executable entry point.
//
// SPDX-License-Identifier: MIT
//

#include "commonCRTStartup.hpp"

extern "C" unsigned long mainCRTStartup(void*)
{
    __security_init_cookie();

    return __commonCRTStartup<decltype(main)>();
}

/* GCC pulls in libgcc's __main; Clang still uses the local startup stub. */
#if !defined(__GNUC__) || defined(__clang__)
extern "C" void __main(void) { }
#endif
