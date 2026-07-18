/*
 * Copyright 2022 Matteo Bruni for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_UTILS_H
#define __WINE_UTILS_H

#include <stdint.h>

#ifdef __REACTOS__
#include <math.h>

static inline const char *debugstr_fourcc(DWORD fourcc)
{
    return wine_dbg_sprintf("%c%c%c%c", (char)fourcc, (char)(fourcc >> 8), (char)(fourcc >> 16), (char)(fourcc >> 24));
}

static inline float reactos_exp2f(float value)
{
    return powf(2.0f, value);
}

static inline float reactos_log2f(float value)
{
    return logf(value) / 0.69314718055994530942f;
}

#define exp2f reactos_exp2f
#define log2f reactos_log2f
#endif

static inline uint32_t read_u32(const char **ptr)
{
    uint32_t r;

    memcpy(&r, *ptr, sizeof(r));
    *ptr += sizeof(r);
    return r;
}

void skip_u32_unknown(const char **ptr, unsigned int count);

#endif /* __WINE_UTILS_H */
