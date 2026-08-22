#define STANDALONE
#include <wine/test.h>

extern void func_xaml(void);

const struct test winetest_testlist[] =
{
    { "xaml", func_xaml },
    { 0, 0 }
};
