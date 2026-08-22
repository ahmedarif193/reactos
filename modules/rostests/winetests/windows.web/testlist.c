#define STANDALONE
#include <wine/test.h>

extern void func_web(void);

const struct test winetest_testlist[] =
{
    { "web", func_web },
    { 0, 0 }
};
