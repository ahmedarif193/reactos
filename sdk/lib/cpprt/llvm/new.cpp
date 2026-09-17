/*
 * PROJECT:     ReactOS C++ runtime library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Dynamic allocation support for the LLVM C++ ABI
 * COPYRIGHT:   Copyright 2026 ReactOS RISC-V64 contributors
 *              Copyright 2026 Ahmed ARIF
 */

#include <new>
#include <stdlib.h>

static new_handler RuntimeNewHandler;

new_handler set_new_handler(new_handler Handler) throw()
{
    return __atomic_exchange_n(&RuntimeNewHandler, Handler, __ATOMIC_ACQ_REL);
}

static void *
RuntimeAllocate(std::size_t Size)
{
    if (Size == 0)
        Size = 1;

    for (;;)
    {
        if (void *Pointer = malloc(Size))
            return Pointer;

        new_handler Handler =
            __atomic_load_n(&RuntimeNewHandler, __ATOMIC_ACQUIRE);
        if (Handler == NULL)
            throw bad_alloc();

        Handler();
    }
}

const std::nothrow_t std::nothrow;

void *operator new(std::size_t Size)
{
    return RuntimeAllocate(Size);
}

void *operator new[](std::size_t Size)
{
    return RuntimeAllocate(Size);
}

void *operator new(std::size_t Size, const std::nothrow_t &) throw()
{
    try
    {
        return RuntimeAllocate(Size);
    }
    catch (...)
    {
        return NULL;
    }
}

void *operator new[](std::size_t Size, const std::nothrow_t &) throw()
{
    try
    {
        return RuntimeAllocate(Size);
    }
    catch (...)
    {
        return NULL;
    }
}

void operator delete(void *Pointer) throw()
{
    free(Pointer);
}

void operator delete[](void *Pointer) throw()
{
    free(Pointer);
}

void operator delete(void *Pointer, const std::nothrow_t &) throw()
{
    free(Pointer);
}

void operator delete[](void *Pointer, const std::nothrow_t &) throw()
{
    free(Pointer);
}
