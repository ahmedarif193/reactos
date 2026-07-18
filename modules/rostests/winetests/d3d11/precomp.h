#pragma once

#include <stdarg.h>
#include <stdio.h>

static inline const char *wine_dbg_sprintf(const char *format, ...)
{
    static char buffer[256];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}
