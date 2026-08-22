#define STANDALONE
#include <wine/test.h>

extern void func_systemid(void);

const struct test winetest_testlist[] =
{
    { "systemid", func_systemid },
    { 0, 0 }
};
