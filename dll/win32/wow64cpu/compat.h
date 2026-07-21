/* ReactOS compatibility definitions for the Wine WoW64 sources. */

#pragma once

#ifndef RTL_CONSTANT_STRING
#define RTL_CONSTANT_STRING(string) \
    { sizeof(string) - sizeof((string)[0]), sizeof(string), (WCHAR *)(string) }
#endif
