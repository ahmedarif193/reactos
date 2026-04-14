/*
 * Copyright (C) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * wim_io.h - UTF conversion and time helpers (pure C)
 *
 */

#ifndef WIM_IO_H
#define WIM_IO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
typedef int64_t wim_off_t;

static inline int
wim_fseeko(FILE* file, wim_off_t offset, int whence)
{
    return _fseeki64(file, offset, whence);
}

static inline wim_off_t
wim_ftello(FILE* file)
{
    return _ftelli64(file);
}

static inline struct tm*
wim_gmtime_utc(const time_t* timep, struct tm* result)
{
    struct tm* tmp;

    if (!timep || !result)
        return NULL;

    tmp = gmtime(timep);
    if (!tmp)
        return NULL;

    *result = *tmp;
    return result;
}
#else
#include <sys/types.h>
typedef off_t wim_off_t;

static inline int
wim_fseeko(FILE* file, wim_off_t offset, int whence)
{
    return fseeko(file, offset, whence);
}

static inline wim_off_t
wim_ftello(FILE* file)
{
    return ftello(file);
}

static inline struct tm*
wim_gmtime_utc(const time_t* timep, struct tm* result)
{
    return gmtime_r(timep, result);
}
#endif

/* UTF conversion - caller frees returned pointers */
int utf8_to_utf16le(const char* utf8, uint16_t** out, size_t* out_len);
char* utf16le_to_utf8(const uint16_t* utf16, size_t len);

/* Time conversion */
uint64_t unix_to_filetime(time_t t);
time_t filetime_to_unix(uint64_t ft);
char* filetime_to_string(uint64_t ft);

#endif /* WIM_IO_H */
