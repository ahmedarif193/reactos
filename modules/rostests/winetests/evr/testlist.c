#define STANDALONE
#include <wine/test.h>

extern void func_evr(void);

const struct test winetest_testlist[] =
{
    { "evr", func_evr },
    { 0, 0 }
};
