/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Fixed-width integer typedefs for the self-contained supplicant.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Avoids <stdint.h>, which is unavailable in some kernel build configurations;
 * define RSNA_USE_STDINT to alias the standard types instead.
 */

#ifndef _RSNA_TYPES_H_
#define _RSNA_TYPES_H_

#include <stddef.h>

#if defined(RSNA_USE_STDINT) || defined(_RSNA_HOST_TEST)
#include <stdint.h>
typedef uint8_t   rsna_u8;
typedef uint16_t  rsna_u16;
typedef uint32_t  rsna_u32;
typedef uint64_t  rsna_u64;
typedef int32_t   rsna_i32;
#else
typedef unsigned char       rsna_u8;
typedef unsigned short      rsna_u16;
typedef unsigned int        rsna_u32;
typedef unsigned long long  rsna_u64;
typedef int                 rsna_i32;
#endif

typedef size_t  rsna_size;

#ifndef RSNA_NULL
#define RSNA_NULL ((void *)0)
#endif

#endif /* _RSNA_TYPES_H_ */
