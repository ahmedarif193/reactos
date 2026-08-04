#define STANDALONE
#include <wine/test.h>

extern void func_graphics(void);

const struct test winetest_testlist[] =
{
    { "graphics", func_graphics },
    { 0, 0 }
};
