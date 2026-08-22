#define STANDALONE
#include <wine/test.h>

extern void func_mfsrcsnk(void);

const struct test winetest_testlist[] =
{
    { "mfsrcsnk", func_mfsrcsnk },
    { 0, 0 }
};
