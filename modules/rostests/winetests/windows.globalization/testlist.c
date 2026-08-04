#define STANDALONE
#include <wine/test.h>

extern void func_globalization(void);

const struct test winetest_testlist[] =
{
    { "globalization", func_globalization },
    { 0, 0 }
};
