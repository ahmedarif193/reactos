#define STANDALONE
#include <wine/test.h>

extern void func_mfplat(void);

const struct test winetest_testlist[] =
{
    { "mfplat", func_mfplat },
    { 0, 0 }
};
