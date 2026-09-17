/*
 * PROJECT:     ReactOS C++ runtime library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ReactOS exception base support for the LLVM C++ ABI
 * COPYRIGHT:   Copyright 2026 ReactOS RISC-V64 contributors
 *              Copyright 2026 Ahmed ARIF
 */

#include <exception>
#include <stdlib.h>
#include <string.h>

exception::exception() throw()
    : _name(NULL), _do_free(0)
{
}

exception::exception(const char * const &Name) throw()
    : _name(NULL), _do_free(0)
{
    if (Name != NULL)
    {
        size_t Length = strlen(Name) + 1;
        char *Copy = static_cast<char *>(malloc(Length));

        if (Copy != NULL)
        {
            memcpy(Copy, Name, Length);
            _name = Copy;
            _do_free = 1;
        }
    }
}

exception::exception(const char * const &Name, int) throw()
    : _name(Name), _do_free(0)
{
}

exception::~exception() throw()
{
    if (_do_free)
        free(const_cast<char *>(_name));
}

const char *exception::what() const throw()
{
    return _name != NULL ? _name : "Unknown exception";
}
