/*
 * PROJECT:     ReactOS Raspberry Pi 5 graphics validation
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Run LLVM/MinGW C++ constructors from the UCRT startup path
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <stddef.h>

typedef void (__cdecl *GLMARK2_CONSTRUCTOR)(void);

extern GLMARK2_CONSTRUCTOR __CTOR_LIST__[];

static void __cdecl
Glmark2RunConstructors(void)
{
    size_t Count;
    static int Initialized;

    if (Initialized)
        return;

    Initialized = 1;
    Count = (size_t)__CTOR_LIST__[0];
    if (Count == (size_t)-1)
    {
        for (Count = 0; __CTOR_LIST__[Count + 1] != NULL; ++Count)
            ;
    }

    while (Count != 0)
        __CTOR_LIST__[Count--]();
}

/* LLVM/MinGW emits the upstream C++ initializers in .ctors, whereas the
 * ReactOS UCRT startup walks .CRT$XC*.  Bridge the two conventions without
 * modifying glmark2 itself or selecting the legacy MSVCRT entry point. */
__attribute__((section(".CRT$XCU"), used))
static GLMARK2_CONSTRUCTOR Glmark2ConstructorBridge = Glmark2RunConstructors;
