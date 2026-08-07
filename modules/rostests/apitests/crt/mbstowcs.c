/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for mbstowcs
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <stdio.h>
#include <stdlib.h>
#include <specstrings.h>

#define StrROS "ReactOS"
#define LStrROS L"ReactOS"

START_TEST(mbstowcs)
{
    size_t len;
    wchar_t out[ARRAYSIZE(LStrROS)];

    len = mbstowcs(NULL, StrROS, 0);
    ok_eq_size(len, 7);
    len = mbstowcs(NULL, StrROS, 0);
    ok_eq_size(len, 7);
    len = mbstowcs(NULL, StrROS, ARRAYSIZE(out));
    ok_eq_size(len, 7);
    len = mbstowcs(NULL, StrROS, ARRAYSIZE(out));
    ok_eq_size(len, 7);
    len = mbstowcs(out, StrROS, ARRAYSIZE(out));
    ok_eq_size(len, 7);
    ok_wstr(out, LStrROS);
    memset(out, 0, sizeof(out));
    len = mbstowcs(out, StrROS, ARRAYSIZE(out));
    ok_eq_size(len, 7);
    ok_wstr(out, LStrROS);
}
