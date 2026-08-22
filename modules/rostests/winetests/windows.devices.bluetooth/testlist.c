#define STANDALONE
#include <wine/test.h>

extern void func_bluetooth(void);

const struct test winetest_testlist[] =
{
    { "bluetooth", func_bluetooth },
    { 0, 0 }
};
