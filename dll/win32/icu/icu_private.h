/*
 * PROJECT:     ReactOS International Components for Unicode
 * FILE:        dll/win32/icu/icu_private.h
 * PURPOSE:     ICU C API types used by the character set converters
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stdint.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>

/* Layouts and values follow the published ICU C headers (unicode/ucnv.h,
 * unicode/ucnv_err.h, unicode/utypes.h) that applications compile against. */

#define U_EXPORT2 __cdecl

typedef WCHAR UChar;
typedef int32_t UChar32;
typedef int8_t UBool;

typedef enum UErrorCode
{
    U_USING_DEFAULT_WARNING = -127,
    U_AMBIGUOUS_ALIAS_WARNING = -122,
    U_ZERO_ERROR = 0,
    U_ILLEGAL_ARGUMENT_ERROR = 1,
    U_MISSING_RESOURCE_ERROR = 2,
    U_INVALID_FORMAT_ERROR = 3,
    U_FILE_ACCESS_ERROR = 4,
    U_INTERNAL_PROGRAM_ERROR = 5,
    U_MESSAGE_PARSE_ERROR = 6,
    U_MEMORY_ALLOCATION_ERROR = 7,
    U_INDEX_OUTOFBOUNDS_ERROR = 8,
    U_PARSE_ERROR = 9,
    U_INVALID_CHAR_FOUND = 10,
    U_TRUNCATED_CHAR_FOUND = 11,
    U_ILLEGAL_CHAR_FOUND = 12,
    U_INVALID_TABLE_FORMAT = 13,
    U_INVALID_TABLE_FILE = 14,
    U_BUFFER_OVERFLOW_ERROR = 15,
    U_UNSUPPORTED_ERROR = 16
} UErrorCode;

#define U_SUCCESS(x) ((x) <= U_ZERO_ERROR)
#define U_FAILURE(x) ((x) > U_ZERO_ERROR)

typedef enum UConverterCallbackReason
{
    UCNV_UNASSIGNED = 0,
    UCNV_ILLEGAL = 1,
    UCNV_IRREGULAR = 2,
    UCNV_RESET = 3,
    UCNV_CLOSE = 4,
    UCNV_CLONE = 5
} UConverterCallbackReason;

/* The "i" context makes the substituting callbacks stop on illegal input. */
#define UCNV_SUB_STOP_ON_ILLEGAL "i"

typedef struct UConverter UConverter;

typedef struct UConverterToUnicodeArgs
{
    uint16_t size;
    UBool flush;
    UConverter *converter;
    const char *source;
    const char *sourceLimit;
    UChar *target;
    const UChar *targetLimit;
    int32_t *offsets;
} UConverterToUnicodeArgs;

typedef struct UConverterFromUnicodeArgs
{
    uint16_t size;
    UBool flush;
    UConverter *converter;
    const UChar *source;
    const UChar *sourceLimit;
    char *target;
    const char *targetLimit;
    int32_t *offsets;
} UConverterFromUnicodeArgs;

typedef void (U_EXPORT2 *UConverterToUCallback)(const void *context,
                                                UConverterToUnicodeArgs *args,
                                                const char *codeUnits,
                                                int32_t length,
                                                UConverterCallbackReason reason,
                                                UErrorCode *pErrorCode);

typedef void (U_EXPORT2 *UConverterFromUCallback)(const void *context,
                                                  UConverterFromUnicodeArgs *args,
                                                  const UChar *codeUnits,
                                                  int32_t length,
                                                  UChar32 codePoint,
                                                  UConverterCallbackReason reason,
                                                  UErrorCode *pErrorCode);
