/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_io.c - UTF conversion and time helpers (pure C)
 *
 */

#define _POSIX_C_SOURCE 200809L

#include "wim_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ================================================================
 *  UTF-8 <-> UTF-16LE
 * ================================================================ */

int utf8_to_utf16le(const char* utf8, uint16_t** out, size_t* out_len)
{
    if (!utf8 || !out || !out_len)
        return -1;

    size_t slen = strlen(utf8);
    /* Worst case: each byte becomes one uint16_t */
    uint16_t* buf = (uint16_t*)malloc((slen + 1) * sizeof(uint16_t));
    if (!buf)
        return -1;

    const uint8_t* p = (const uint8_t*)utf8;
    const uint8_t* end = p + slen;
    size_t count = 0;

    while (p < end) {
        uint32_t cp;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = (*p++ & 0x1F) << 6;
            if (p < end) cp |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
            cp = (*p++ & 0x0F) << 12;
            if (p < end) cp |= (*p++ & 0x3F) << 6;
            if (p < end) cp |= (*p++ & 0x3F);
        } else {
            /* Skip 4-byte sequences (outside BMP) */
            p++;
            if (p < end && (*p & 0xC0) == 0x80) p++;
            if (p < end && (*p & 0xC0) == 0x80) p++;
            if (p < end && (*p & 0xC0) == 0x80) p++;
            cp = 0xFFFD;
        }
        buf[count++] = (uint16_t)(cp & 0xFFFF);
    }

    *out = buf;
    *out_len = count;
    return 0;
}

char* utf16le_to_utf8(const uint16_t* utf16, size_t len)
{
    if (!utf16 && len > 0)
        return NULL;

    /* Worst case: each uint16_t becomes 3 UTF-8 bytes */
    char* buf = (char*)malloc(len * 3 + 1);
    if (!buf)
        return NULL;

    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        uint16_t c = utf16[i];
        if (c < 0x80) {
            buf[pos++] = (char)c;
        } else if (c < 0x800) {
            buf[pos++] = (char)(0xC0 | (c >> 6));
            buf[pos++] = (char)(0x80 | (c & 0x3F));
        } else {
            buf[pos++] = (char)(0xE0 | (c >> 12));
            buf[pos++] = (char)(0x80 | ((c >> 6) & 0x3F));
            buf[pos++] = (char)(0x80 | (c & 0x3F));
        }
    }
    buf[pos] = '\0';
    return buf;
}

/* ================================================================
 *  Time conversion
 * ================================================================ */

static const uint64_t FILETIME_UNIX_EPOCH_DIFF = 11644473600ULL;

uint64_t unix_to_filetime(time_t t)
{
    return ((uint64_t)t + FILETIME_UNIX_EPOCH_DIFF) * 10000000ULL;
}

time_t filetime_to_unix(uint64_t ft)
{
    if (ft < FILETIME_UNIX_EPOCH_DIFF * 10000000ULL)
        return 0;
    return (time_t)(ft / 10000000ULL - FILETIME_UNIX_EPOCH_DIFF);
}

char* filetime_to_string(uint64_t ft)
{
    time_t t = filetime_to_unix(ft);
    struct tm tm_val;
    gmtime_r(&t, &tm_val);
    char* buf = (char*)malloc(64);
    if (!buf)
        return NULL;
    strftime(buf, 64, "%Y-%m-%dT%H:%M:%SZ", &tm_val);
    return buf;
}
