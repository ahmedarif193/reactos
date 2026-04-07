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
#include <time.h>

/* UTF conversion - caller frees returned pointers */
int utf8_to_utf16le(const char* utf8, uint16_t** out, size_t* out_len);
char* utf16le_to_utf8(const uint16_t* utf16, size_t len);

/* Time conversion */
uint64_t unix_to_filetime(time_t t);
time_t filetime_to_unix(uint64_t ft);
char* filetime_to_string(uint64_t ft);

#endif /* WIM_IO_H */
