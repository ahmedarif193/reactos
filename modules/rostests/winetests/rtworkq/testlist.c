#define STANDALONE
#include <wine/test.h>

extern void func_rtworkq(void);

const struct test winetest_testlist[] =
{
    { "rtworkq", func_rtworkq },
    { 0, 0 }
};
