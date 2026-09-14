/* SPDX-License-Identifier: GPL-2.0-or-later */
#define STANDALONE
#include <apitest.h>

extern void func_arm64_chpe(void);
extern void func_wow64_startup(void);
extern void func_LdrFindResource_U(void);
extern void func_LdrLoadDll(void);
#if defined(_M_IX86) || defined(_M_AMD64)
extern void func_NtContinue(void);
extern void func_UserModeException(void);
#endif

const struct test winetest_testlist[] =
{
    { "arm64_chpe", func_arm64_chpe },
    { "wow64_startup", func_wow64_startup },
    { "LdrFindResource_U", func_LdrFindResource_U },
    { "LdrLoadDll", func_LdrLoadDll },
#if defined(_M_IX86) || defined(_M_AMD64)
    { "NtContinue", func_NtContinue },
    { "UserModeException", func_UserModeException },
#endif
    { 0, 0 }
};
