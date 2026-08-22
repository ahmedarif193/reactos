#define STANDALONE
#include <wine/test.h>

extern void func_gamebar(void);

const struct test winetest_testlist[] =
{
    { "gamebar", func_gamebar },
    { 0, 0 }
};
