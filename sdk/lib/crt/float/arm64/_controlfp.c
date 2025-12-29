/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Minimal ARM64 implementation of _controlfp/_control87
 */

#include <precomp.h>

unsigned int CDECL _control87(unsigned int newval, unsigned int mask)
{
    UNREFERENCED_PARAMETER(newval);
    UNREFERENCED_PARAMETER(mask);
    return 0;
}

unsigned int CDECL _controlfp(unsigned int newval, unsigned int mask)
{
    return _control87(newval, mask);
}
