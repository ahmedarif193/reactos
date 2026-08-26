/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for fmod and fmodf
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#if !defined(_CRTBLD) && !defined(_M_IX86)
#define _CRTBLD
#endif
#include "math_helpers.h"

#ifdef _MSC_VER
#pragma function(fmod)
#ifndef _M_IX86
#pragma function(fmodf)
#endif
#endif

typedef struct _FMOD_TEST_ENTRY
{
    UINT64 x;
    UINT64 y;
    UINT64 result;
} FMOD_TEST_ENTRY;

static const FMOD_TEST_ENTRY s_fmod_tests[] =
{
    { 0x0000000000000000, 0x4076800000000000, 0x0000000000000000 }, /*  0 % 360 =  0 */
    { 0x8000000000000000, 0x4076800000000000, 0x8000000000000000 }, /* -0 % 360 = -0 */
    { 0x4077200000000000, 0x4076800000000000, 0x4024000000000000 }, /*  370 % 360 =  10 */
    { 0xc077200000000000, 0x4076800000000000, 0xc024000000000000 }, /* -370 % 360 = -10 */
    { 0x4016000000000000, 0x4000000000000000, 0x3ff8000000000000 }, /*  5.5 % 2 =  1.5 */
    { 0xc016000000000000, 0x4000000000000000, 0xbff8000000000000 }, /* -5.5 % 2 = -1.5 */
    { 0x4016000000000000, 0xc000000000000000, 0x3ff8000000000000 }, /*  5.5 % -2 = 1.5 */
};

static void
Test_fmod(void)
{
    unsigned int i;

    for (i = 0; i < _countof(s_fmod_tests); ++i)
    {
        double x = u64_to_dbl(s_fmod_tests[i].x);
        double y = u64_to_dbl(s_fmod_tests[i].y);
        double result = fmod(x, y);
        ok_eq_dbl_exact("fmod", s_fmod_tests[i].x, result, s_fmod_tests[i].result);
    }
}

#ifndef _M_IX86
static void
Test_fmodf(void)
{
    static const struct
    {
        UINT32 x;
        UINT32 y;
        UINT32 result;
    } tests[] =
    {
        { 0x00000000, 0x43b40000, 0x00000000 }, /*  0 % 360 =  0 */
        { 0x80000000, 0x43b40000, 0x80000000 }, /* -0 % 360 = -0 */
        { 0x43b90000, 0x43b40000, 0x41200000 }, /*  370 % 360 =  10 */
        { 0xc3b90000, 0x43b40000, 0xc1200000 }, /* -370 % 360 = -10 */
        { 0x40b00000, 0x40000000, 0x3fc00000 }, /*  5.5 % 2 =  1.5 */
        { 0xc0b00000, 0x40000000, 0xbfc00000 }, /* -5.5 % 2 = -1.5 */
    };
    unsigned int i;

    for (i = 0; i < _countof(tests); ++i)
    {
        float x = u32_to_flt(tests[i].x);
        float y = u32_to_flt(tests[i].y);
        float result = fmodf(x, y);
        ok_eq_flt_exact("fmodf", tests[i].x, result, tests[i].result);
    }
}
#endif

START_TEST(fmod)
{
    Test_fmod();
#ifndef _M_IX86
    Test_fmodf();
#endif
}
