#define STANDALONE
#include <wine/test.h>

extern void func_onlineid(void);

const struct test winetest_testlist[] =
{
    { "onlineid", func_onlineid },
    { 0, 0 }
};
